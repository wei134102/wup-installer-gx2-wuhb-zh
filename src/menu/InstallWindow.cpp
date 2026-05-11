/****************************************************************************
 * Copyright (C) 2011 Dimok
 * Copyright (C) 2012 Cyan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#include "Application.h"
#include "InstallWindow.h"
#include "utils/StringTools.h"
#include "common/common.h"
#include "system/power.h"
#include <cstdio>
#include <string>
#include <coreinit/mcp.h>
#include <coreinit/memory.h>
#include <coreinit/ios.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <cstring>

#define MCP_COMMAND_INSTALL_ASYNC   0x81
//! 已装在目标设备上，跳过安装（InstallProcess 内使用，勿与普通成功混淆）
#define INSTALL_RESULT_SKIP_DUPLICATE 88
#define MAX_INSTALL_PATH_LENGTH     0x27F

static int installCompleted = 0;
static u32 installError = 0;

extern "C" MCPError MCP_GetLastRawError(void);

//! 与官方安装目标一致：从 MCP 设备表取当前「选 NAND / 选 USB」对应的存储根路径（不写死 storage_usb 等卷名）。
static const char *McpStripVolPrefix(const char *p)
{
	if (!p)
		return "";
	if (strncmp(p, "fs:/vol/", 8) == 0)
		return p + 8;
	if (strncmp(p, "/vol/", 5) == 0)
		return p + 5;
	return p;
}

static const char *SkipLeadingSlashes(const char *p)
{
	if (!p)
		return "";
	while (*p == '/' || *p == '\\')
		++p;
	return p;
}

//! title 路径是否落在 MCP 给出的安装卷根之下（与 MCP_InstallSetTarget* 选中的存储一致）
static bool TitlePathUnderMcpRoot(const char *titlePath, const char *storageRoot)
{
	const char *pn = SkipLeadingSlashes(McpStripVolPrefix(titlePath));
	const char *rn = SkipLeadingSlashes(McpStripVolPrefix(storageRoot));
	if (!pn[0] || !rn[0])
		return false;
	size_t lr = strlen(rn);
	while (lr > 0 && (rn[lr - 1] == '/' || rn[lr - 1] == '\\'))
		--lr;
	if (lr == 0)
		return false;
	if (strncmp(pn, rn, lr) != 0)
		return false;
	return pn[lr] == '\0' || pn[lr] == '/';
}

static bool LookupMcpInstallStorageRoot(unsigned int mcpHandle, bool installToUsb, char *outRoot, size_t outRootBytes)
{
	if (!outRoot || outRootBytes < 2)
		return false;
	outRoot[0] = '\0';
	const uint32_t kMaxDev = 32u;
	const uint32_t listBytes = kMaxDev * (uint32_t)sizeof(MCPDevice);
	MCPDevice *devs = (MCPDevice *)OSAllocFromSystem(listBytes, 0x40);
	if (!devs)
		return false;
	memset(devs, 0, listBytes);
	int num = 0;
	MCPError err = MCP_FullDeviceList((int)mcpHandle, &num, devs, listBytes);
	bool got = false;
	if (err == 0 && num > 0)
	{
		const int n = num > (int)kMaxDev ? (int)kMaxDev : num;
		for (int i = 0; i < n; ++i)
		{
			const char *ty = devs[i].type;
			const char *rp = devs[i].path;
			if (!rp || !rp[0])
				continue;
			bool want = false;
			if (installToUsb)
			{
				if (ty[0] == 'u' && ty[1] == 's' && ty[2] == 'b')
					want = true;
			}
			else
			{
				if (ty[0] == 'm' && ty[1] == 'l' && ty[2] == 'c')
					want = true;
			}
			if (want)
			{
				snprintf(outRoot, outRootBytes, "%s", rp);
				got = true;
				break;
			}
		}
	}
	OSFreeToSystem(devs);
	return got && outRoot[0] != '\0';
}

static void* IosInstallCallback(IOSError errorCode, void * priv_data)
{
	installError = errorCode;
	installCompleted = 1;
	return 0;
}

static const char *const kInstallLogPaths[] = {
	"fs:/vol/external01/wup_install_gx2.log",
	"fs:/vol/external01/install/wup_install_gx2.log",
	"/vol/app_sd/wup_install_gx2.log",
	nullptr
};

void InstallWindow::InitInstallLogAtStartup()
{
	static bool sDone = false;
	if (sDone)
		return;
	sDone = true;

	for (int i = 0; kInstallLogPaths[i]; ++i)
	{
		FILE *probe = fopen(kInstallLogPaths[i], "rb");
		if (!probe)
			continue;
		std::fclose(probe);

		char bakPath[384];
		snprintf(bakPath, sizeof(bakPath), "%s.bak", kInstallLogPaths[i]);
		::remove(bakPath);
		if (::rename(kInstallLogPaths[i], bakPath) != 0)
		{
			::remove(kInstallLogPaths[i]);
		}
		break;
	}
}

void InstallWindow::AppendInstallLog(const std::string & line)
{
	for (int i = 0; kInstallLogPaths[i]; ++i)
	{
		FILE *f = fopen(kInstallLogPaths[i], "a");
		if (f)
		{
			std::fprintf(f, "[%llu] %s\n", (unsigned long long)OSGetTime(), line.c_str());
			std::fclose(f);
			return;
		}
	}
}

void InstallWindow::OnAbortCurrentInstall(GuiElement *, int)
{
	abortCurrentInstall = true;
}

void InstallWindow::OnFailContinueYes(GuiElement *, int)
{
	failContinueChoice = 1;
}

void InstallWindow::OnFailContinueNo(GuiElement *, int)
{
	failContinueChoice = 2;
}

void InstallWindow::OnDuplicateSkipOk(GuiElement *, int)
{
	duplicateSkipAck = 1;
	__sync_synchronize();
}

InstallWindow::InstallWindow(CFolderList * list)
	: GuiFrame(0, 0)
	, CThread(CThread::eAttributeAffCore0 | CThread::eAttributePinnedAff)
	, folderList(list)
	, abortCurrentInstall(false)
	, failContinueChoice(0)
	, duplicateSkipAck(0)
{   
	mainWindow = Application::instance()->getMainWindow();
	
	folderCount = folderList->GetSelectedCount();
	
	if(folderCount > 0)
	{
		std::string message = fmt("总共 %d 个软件", folderCount);
		messageBox = new MessageBox(MessageBox::BT_YESNO, MessageBox::IT_ICONQUESTION, false);
		messageBox->setTitle("你想要安装这些软件吗:");
		messageBox->setMessage1(message);
		messageBox->messageYesClicked.connect(this, &InstallWindow::OnValidInstallClick);
		messageBox->messageNoClicked.connect(this, &InstallWindow::OnCloseWindow);
	}
	else
	{
		messageBox = new MessageBox(MessageBox::BT_OK, MessageBox::IT_ICONEXCLAMATION, false);
		messageBox->setTitle("没有选中的内容。");
		messageBox->setMessage1("返回到选择界面");
		messageBox->messageOkClicked.connect(this, &InstallWindow::OnCloseWindow);
	}
	
	drcFrame = new GuiFrame(0, 0);
	drcFrame->setEffect(EFFECT_FADE, 10, 255);
	drcFrame->setState(GuiElement::STATE_DISABLED);
	drcFrame->effectFinished.connect(this, &InstallWindow::OnOpenEffectFinish);
	drcFrame->append(messageBox);
	
	mainWindow->append(drcFrame);
}

InstallWindow::~InstallWindow()
{
	drcFrame->remove(messageBox);
	mainWindow->remove(drcFrame);
	delete drcFrame;
	delete messageBox;
}

void InstallWindow::OnValidInstallClick(GuiElement * element, int val)
{
	messageBox->messageYesClicked.disconnect(this);
	messageBox->messageNoClicked.disconnect(this);
	messageBox->reload("你想安装在哪里?", "", "", MessageBox::BT_DEST, MessageBox::IT_ICONQUESTION);
	messageBox->messageYesClicked.connect(this, &InstallWindow::OnDestinationChoice);
	messageBox->messageNoClicked.connect(this, &InstallWindow::OnDestinationChoice);
}

void InstallWindow::OnDestinationChoice(GuiElement * element, int choice)
{
	if(choice == MessageBox::MR_YES)
		target = NAND;
	else
		target = USB;
	
	messageBox->messageYesClicked.disconnect(this);
	messageBox->messageNoClicked.disconnect(this);
	
	startInstalling();
}

void InstallWindow::executeThread()
{
	Application::instance()->exitDisable();
	OSEnableHomeButtonMenu(false);
	
	canceled = false;
	
	bool APD_enabled = isEnabledAutoPowerDown();
	if(APD_enabled)
		disableAutoPowerDown();
	
	int total = folderList->GetSelectedCount();
	int pos = 1;

	AppendInstallLog(strfmt("===== 批量安装开始 共 %d 项 =====", total));
	
	while(pos <= total && !canceled)
	{
		InstallProcess(pos, total);
		
		if(pos < total)
		{
			int time = 6;
			u64 startTime = OSGetTime();
			u32 passedMs = 0;
			
			while(time && !canceled)
			{
				passedMs = OSTicksToMilliseconds(OSGetTime() - startTime);
				
				if(passedMs >= 1000)
				{
					time--;
					startTime = OSGetTime();
					messageBox->setMessage2(fmt("%d秒后开始安装下个软件", time));
				}
			}
			
			messageBox->messageCancelClicked.disconnect(this);
		}
		
		pos++;
	}
	
	if(APD_enabled)
		enableAutoPowerDown();
	
	OSEnableHomeButtonMenu(true);
	Application::instance()->exitEnable();

	AppendInstallLog(strfmt("===== 批量安装结束 canceled=%d =====", canceled ? 1 : 0));
}

void InstallWindow::InstallProcess(int pos, int total)
{
	int index = folderList->GetFirstSelected();
	
	std::string title = fmt("安装中... (%d/%d)", pos, total);
	std::string gameName = folderList->GetName(index);

	abortCurrentInstall = false;
	messageBox->messageCancelClicked.disconnect(this);
	messageBox->reload(title, gameName, "按「取消」可中止本项（调用 MCP 中止）", MessageBox::BT_CANCEL, MessageBox::IT_ICONINFORMATION, true, "0.0 %");
	messageBox->messageCancelClicked.connect(this, &InstallWindow::OnAbortCurrentInstall);

	AppendInstallLog(strfmt("START [%d/%d] %s", pos, total, gameName.c_str()));
	
	/////////////////////////////
	// install process
	/////////////////////////////
	
	int result = 0;
	installCompleted = 0;
	installError = 0;
	
	//!---------------------------------------------------
	//! This part of code originates from Crediars MCP patcher assembly code
	//! it is just translated to C
	//!---------------------------------------------------
	unsigned int mcpHandle = MCP_Open();
	if(mcpHandle == 0)
	{
		messageBox->reload("安装失败", gameName, "无法打开MCP。", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
		
		result = -1;
	}
	else
	{
        char installPath[256];
		unsigned int * mcpInstallInfo = (unsigned int *)OSAllocFromSystem(0x24, 0x40);
		char * mcpInstallPath = (char *)OSAllocFromSystem(MAX_INSTALL_PATH_LENGTH, 0x40);
		IOSVec * mcpPathInfoVector = (IOSVec *)OSAllocFromSystem(0x0C, 0x40);
		
		do
		{
			if(!mcpInstallInfo || !mcpInstallPath || !mcpPathInfoVector)
			{
				messageBox->reload("安装失败", gameName, "无法分配内存。", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
				result = -2;
				break;
			}
			
			std::string installFolder = folderList->GetPath(index);
			installFolder.erase(0, 19);
			installFolder.insert(0, "/vol/app_sd/");
            
            snprintf(installPath, sizeof(installPath), "%s", installFolder.c_str());
			
			int res = MCP_InstallGetInfo(mcpHandle, installPath, (MCPInstallInfo*)mcpInstallInfo);
			if(res != 0)
			{
				//__os_snprintf(errorText1, sizeof(errorText1), "Error: MCP_InstallGetInfo 0x%08X", MCP_GetLastRawError());
				messageBox->reload(installFolder, gameName, "确认文件夹中有完整的WUP文件。", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
				result = -3;
				break;
			}
			
			u32 titleIdHigh = mcpInstallInfo[0];
			u32 titleIdLow = mcpInstallInfo[1];
			bool spoofFiles = false;
			if ((titleIdHigh == 00050010)
				&&(	   (titleIdLow == 0x10041000)     // JAP Version.bin
					|| (titleIdLow == 0x10041100)     // USA Version.bin
					|| (titleIdLow == 0x10041200)))   // EUR Version.bin
			{
				spoofFiles = true;
				target = NAND;
			}
			
			if (spoofFiles
			   || (titleIdHigh == 0x0005000E)     // game update
			   || (titleIdHigh == 0x00050000)     // game
			   || (titleIdHigh == 0x0005000C)     // DLC
			   || (titleIdHigh == 0x00050002))    // Demo
			{
				res = MCP_InstallSetTargetDevice(mcpHandle, (MCPInstallTarget)(target));
				if(res != 0)
				{
					messageBox->reload("安装失败", gameName, fmt("MCP_InstallSetTargetDevice 0x%08X", MCP_GetLastRawError()), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
					//if (installToUsb)
					//	__os_snprintf(errorText2, sizeof(errorText2), "Possible USB HDD disconnected or failure");
					result = -5;
					break;
				}
				res = MCP_InstallSetTargetUsb(mcpHandle, (MCPInstallTarget)(target));
				if(res != 0)
				{
					messageBox->reload("安装失败", gameName, fmt("MCP_InstallSetTargetUsb 0x%08X", MCP_GetLastRawError()), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
					//if (installToUsb)
					//	__os_snprintf(errorText2, sizeof(errorText2), "Possible USB HDD disconnected or failure");
					result = -6;
					break;
				}

				//! 已安装检测：MCP_GetTitleInfo 按完整 64 位 TID（本体/DLC/更新等为不同 TID）。
				//! 是否与「当前选中的安装目标」同卷：以 MCP_FullDeviceList 返回的 MCPDevice.path 为根（与官方 MCP_InstallSetTarget* 所用存储一致），与 title 的 path 做前缀匹配，不写死 storage_usb 等名。
				{
					uint64_t installTid = ((uint64_t)titleIdHigh << 32) | (uint64_t)titleIdLow;
					MCPTitleListType tinfo;
					memset(&tinfo, 0, sizeof(tinfo));
					MCPError ge = MCP_GetTitleInfo((int32_t)mcpHandle, installTid, &tinfo);

					bool duplicate = false;
					if (ge == 0)
					{
						const char *idev = tinfo.indexedDevice;
						const char *p = tinfo.path;
						const bool installToUsb = (target == USB);
						char storageRoot[0x280];
						const bool haveMcpRoot = LookupMcpInstallStorageRoot(mcpHandle, installToUsb, storageRoot, sizeof(storageRoot));

						auto sub = [](const char *s, const char *needle) -> bool {
							return s && strstr(s, needle) != nullptr;
						};

						if (haveMcpRoot && p && p[0])
						{
							duplicate = TitlePathUnderMcpRoot(p, storageRoot);
						}
						else if (haveMcpRoot && (!p || !p[0]))
						{
							const bool usbI = sub(idev, "usb");
							const bool mlcI = sub(idev, "mlc") || sub(idev, "slc");
							if (installToUsb)
								duplicate = usbI;
							else
								duplicate = mlcI && !usbI;
						}
						else
						{
							//! 设备表不可用时的回退（仍尽量避免误杀）
							const bool usbHint = sub(p, "storage_usb") || sub(idev, "usb");
							const bool mlcHint = sub(p, "storage_mlc") || sub(p, "storage_slc")
								|| sub(idev, "mlc") || sub(idev, "slc");
							if (!usbHint && !mlcHint)
								duplicate = false;
							else if (installToUsb)
								duplicate = usbHint;
							else
								duplicate = !usbHint;
						}

						AppendInstallLog(strfmt(
							"DUPCHK GetTitleInfo=OK tid=%016llx want=%s mcpRootOk=%d dup=%d root=[%s] path=[%s] idx=[%s]",
							(unsigned long long)installTid,
							installToUsb ? "USB" : "NAND",
							haveMcpRoot ? 1 : 0,
							duplicate ? 1 : 0,
							haveMcpRoot ? storageRoot : "-",
							p ? p : "",
							idev ? idev : ""));
					}
					else
					{
						AppendInstallLog(strfmt("DUPCHK GetTitleInfo=%d tid=%016llx (not installed or err) game=%s",
							(int)ge, (unsigned long long)installTid, gameName.c_str()));
					}

					if (duplicate)
					{
						const char *locStr = (target == NAND) ? "NAND(mlc)" : "USB";
						AppendInstallLog(strfmt("SKIP already installed TitleID=%016llx target=%s game=%s",
							(unsigned long long)installTid, locStr, gameName.c_str()));
						messageBox->messageOkClicked.disconnect(this);
						messageBox->messageYesClicked.disconnect(this);
						messageBox->messageNoClicked.disconnect(this);
						messageBox->messageCancelClicked.disconnect(this);
						duplicateSkipAck = 0;
						__sync_synchronize();

						std::string dupBody = fmt("安装目标 %s，本机已存在 Title 0x%016llx（设备:%s），跳过。",
							locStr, (unsigned long long)installTid, tinfo.indexedDevice[0] ? tinfo.indexedDevice : "?");
						const bool batchQueue = (total > 1);
						if (batchQueue)
							dupBody += "\n批量安装：按「确认」立即继续下一项；或等待下方倒计时自动继续。";
						else
							dupBody += "\n按「确认」关闭本提示。";

						messageBox->reload("已安装 跳过", gameName, dupBody,
							MessageBox::BT_OK, MessageBox::IT_ICONINFORMATION);
						messageBox->messageOkClicked.connect(this, &InstallWindow::OnDuplicateSkipOk);

						const u64 dupWaitStart = OSGetTime();
						int prevCountdownSec = -1;
						while (duplicateSkipAck == 0 && !canceled)
						{
							__sync_synchronize();
							if (batchQueue)
							{
								const u32 elapsedMs = OSTicksToMilliseconds(OSGetTime() - dupWaitStart);
								if (elapsedMs >= 3000u)
								{
									duplicateSkipAck = 1;
									__sync_synchronize();
									break;
								}
								const int secLeft = 3 - (int)(elapsedMs / 1000u);
								if (secLeft != prevCountdownSec && secLeft >= 1 && secLeft <= 3)
								{
									prevCountdownSec = secLeft;
									messageBox->setMessage2(dupBody + fmt("\n%d 秒后自动继续下一项…", secLeft));
								}
							}
							usleep(16666);
							OSYieldThread();
						}
						messageBox->messageOkClicked.disconnect(this);
						result = INSTALL_RESULT_SKIP_DUPLICATE;
						break;
					}
				}
				
				mcpInstallInfo[2] = (unsigned int)MCP_COMMAND_INSTALL_ASYNC;
				mcpInstallInfo[3] = (unsigned int)mcpPathInfoVector;
				mcpInstallInfo[4] = (unsigned int)1;
				mcpInstallInfo[5] = (unsigned int)0;
				
				memset(mcpInstallPath, 0, MAX_INSTALL_PATH_LENGTH);
				snprintf(mcpInstallPath, MAX_INSTALL_PATH_LENGTH, installFolder.c_str());
				memset(mcpPathInfoVector, 0, 0x0C);
				
				mcpPathInfoVector->vaddr = mcpInstallPath;
				mcpPathInfoVector->len = (unsigned int)MAX_INSTALL_PATH_LENGTH;
				
				res = IOS_IoctlvAsync(mcpHandle, MCP_COMMAND_INSTALL_ASYNC, 1, 0, mcpPathInfoVector, (IOSAsyncCallbackFn)IosInstallCallback, mcpInstallInfo);
				if(res != 0)
				{
					messageBox->reload("安装失败", gameName, fmt("MCP_InstallTitleAsync 0x%08X", MCP_GetLastRawError()), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
					result = -7;
					break;
				}
				
				u64 lastInstalledSize = (u64)-1;
				int stallIterations = 0;
				//! 约 50ms * 3600 ≈ 3 分钟无字节增长则视为卡死并请求中止
				const int kStallIterationLimit = 3600;
				u64 waitAbortDeadline = 0;

				while(!installCompleted)
				{
					if (abortCurrentInstall && waitAbortDeadline == 0)
					{
						MCP_InstallTitleAbort((int)mcpHandle);
						AppendInstallLog(strfmt("ABORT requested MCP_InstallTitleAbort: %s", gameName.c_str()));
						waitAbortDeadline = OSGetTime();
						abortCurrentInstall = false;
					}

					memset(mcpInstallInfo, 0, 0x24);
					MCP_InstallGetProgress(mcpHandle, (MCPInstallProgress*)mcpInstallInfo);
					
					if(mcpInstallInfo[0] == 1)
					{
						u64 totalSize = ((u64)mcpInstallInfo[3] << 32ULL) | mcpInstallInfo[4];
						u64 installedSize = ((u64)mcpInstallInfo[5] << 32ULL) | mcpInstallInfo[6];
						int percent = (totalSize != 0) ? ((installedSize * 100.0f) / totalSize) : 0;
						
						std::string message = fmt("%0.1f / %0.1f MB (%i", installedSize / (1024.0f * 1024.0f), totalSize / (1024.0f * 1024.0f), percent);
						message += "%)";
						
						messageBox->setProgress(percent);
						messageBox->setProgressBarInfo(message);

						//! 仅在「未完成」时检测停滞，避免 100% 等待 IOS 收尾时误判
						if (totalSize > 0 && installedSize < totalSize)
						{
							if (installedSize == lastInstalledSize)
								stallIterations++;
							else
							{
								stallIterations = 0;
								lastInstalledSize = installedSize;
							}
							if (stallIterations >= kStallIterationLimit)
							{
								MCP_InstallTitleAbort((int)mcpHandle);
								AppendInstallLog(strfmt("STALL timeout MCP_InstallTitleAbort: %s", gameName.c_str()));
								stallIterations = 0;
								if (waitAbortDeadline == 0)
									waitAbortDeadline = OSGetTime();
							}
						}
						else
							stallIterations = 0;
					}

					if (waitAbortDeadline != 0 && OSTicksToMilliseconds(OSGetTime() - waitAbortDeadline) > 120000)
					{
						if (!installCompleted)
						{
							AppendInstallLog(strfmt("WARN abort wait 120s no callback: %s", gameName.c_str()));
							installCompleted = 1;
							installError = 0xEAAAAAAB;
						}
						waitAbortDeadline = 0;
					}
					
					usleep(50000);
				}

				messageBox->messageCancelClicked.disconnect(this);
				waitAbortDeadline = 0;
				
				if(installError != 0)
				{
					if ((installError == 0xFFFCFFE9) && (target == USB))
					{
						messageBox->reload("安装失败", gameName, fmt("0x%08X无法连接 (没有USB设备?)", installError), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						result = -8;
					}
					else
					{
						//__os_snprintf(errorText1, sizeof(errorText1), "Error: install error code 0x%08X", installError);
						if (installError == 0xFFFBF446 || installError == 0xFFFBF43F)
							messageBox->reload("安装失败", gameName, "没有或已损的title.tik文件?", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if (installError == 0xFFFBF441)
							messageBox->reload("安装失败", gameName, "DLC的title.tik可能不正确。", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if (installError == 0xFFFCFFE4)
							messageBox->reload("安装失败", gameName, "可能选中的设备没有足够内存。", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if (installError == 0xFFFFF825)
							messageBox->reload("安装失败", gameName, "SD卡可能已损坏。重新格式化(簇大小选32k)或更换SD卡。", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if ((installError & 0xFFFF0000) == 0xFFFB0000)
							messageBox->reload("安装失败", gameName, "检查WUP是否正确完整。数字版游戏和DLC需要Sig-Patches。", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if (installError == 0xEAAAAAAB)
							messageBox->reload("安装中止", gameName, "已请求中止但长时间未收到 IOS 回调。", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else
							messageBox->reload("安装失败", gameName, fmt("错误代码 0x%08X", installError), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						
						result = -9;
					}
				}
			}
			else
			{
				messageBox->reload("安装失败", gameName, "不是游戏,更新补丁,DLC,试玩版或完整的WUP。", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
				result = -4;
			}
		}
		while(0);
		
		MCP_Close(mcpHandle);
		if(mcpPathInfoVector)
			OSFreeToSystem(mcpPathInfoVector);
		if(mcpInstallPath)
			OSFreeToSystem(mcpInstallPath);
		if(mcpInstallInfo)
			OSFreeToSystem(mcpInstallInfo);
	}
	/////////////////////////////

	messageBox->messageCancelClicked.disconnect(this);
	
	if (result == INSTALL_RESULT_SKIP_DUPLICATE)
	{
		folderList->UnSelect(index);
		messageBox->messageOkClicked.disconnect(this);
		messageBox->messageCancelClicked.disconnect(this);
		//! 不再二次弹出「已跳过」：「已安装 跳过」对话框已说明原因；队列仅切到中性倒计时界面
		if (pos < total)
		{
			messageBox->reload("安装队列", gameName, "6秒后进行下个软件安装", MessageBox::BT_CANCEL, MessageBox::IT_ICONINFORMATION);
			messageBox->messageCancelClicked.connect(this, &InstallWindow::OnInstallProcessCancel);
		}
		else
		{
			OnCloseWindow(this, 0);
		}
	}
	else if(result >= 0)
	{
		AppendInstallLog(strfmt("OK %s", gameName.c_str()));

		if(pos == total)
		{
			messageBox->reload("安装完成", gameName, "", MessageBox::BT_OK, MessageBox::IT_ICONTRUE);
			messageBox->messageOkClicked.connect(this, &InstallWindow::OnCloseWindow);
		}
		else
		{
			messageBox->reload("安装完成", gameName, "6秒后进行下个软件安装", MessageBox::BT_CANCEL, MessageBox::IT_ICONTRUE);
			messageBox->messageCancelClicked.connect(this, &InstallWindow::OnInstallProcessCancel);
		}
		
		folderList->UnSelect(index);
	}
	else
	{
		messageBox->messageOkClicked.disconnect(this);
		messageBox->messageYesClicked.disconnect(this);
		messageBox->messageNoClicked.disconnect(this);

		AppendInstallLog(strfmt("FAIL result=%d ios_err=0x%08X game=%s", result, installError, gameName.c_str()));

		failContinueChoice = 0;
		messageBox->reload("安装失败", gameName, "是否继续安装队列中的下一项？", MessageBox::BT_YESNO, MessageBox::IT_ICONERROR);
		messageBox->messageYesClicked.connect(this, &InstallWindow::OnFailContinueYes);
		messageBox->messageNoClicked.connect(this, &InstallWindow::OnFailContinueNo);
		while (failContinueChoice == 0 && !canceled)
			usleep(20000);
		messageBox->messageYesClicked.disconnect(this);
		messageBox->messageNoClicked.disconnect(this);

		if (failContinueChoice == 0)
			failContinueChoice = 2;

		if (failContinueChoice == 1)
		{
			folderList->UnSelect(index);
			AppendInstallLog(strfmt("CONTINUE queue after fail, skipped: %s", gameName.c_str()));
		}
		else
		{
			canceled = true;
			folderList->UnSelectAll();
			messageBox->reload("已结束", "安装队列已取消", "", MessageBox::BT_OK, MessageBox::IT_ICONINFORMATION);
			messageBox->messageOkClicked.connect(this, &InstallWindow::OnCloseWindow);
		}
	}
}

void InstallWindow::OnInstallProcessCancel(GuiElement *element, int val)
{
	canceled = true;
	folderList->UnSelectAll();
	OnCloseWindow(this, 0);
}

void InstallWindow::OnCloseWindow(GuiElement * element, int val)
{
	messageBox->setEffect(EFFECT_FADE, -10, 255);
	messageBox->setState(GuiElement::STATE_DISABLED);
	messageBox->effectFinished.connect(this, &InstallWindow::OnWindowClosed);
}

void InstallWindow::OnWindowClosed(GuiElement *element)
{
	messageBox->effectFinished.disconnect(this);
	installWindowClosed(this);
	
	AsyncDeleter::pushForDelete(this);
}

void InstallWindow::OnOpenEffectFinish(GuiElement *element)
{
	element->effectFinished.disconnect(this);
	element->clearState(GuiElement::STATE_DISABLED);
}

void InstallWindow::OnCloseEffectFinish(GuiElement *element)
{
	remove(element);
	AsyncDeleter::pushForDelete(element);
}
