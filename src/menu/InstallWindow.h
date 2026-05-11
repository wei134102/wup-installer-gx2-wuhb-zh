#ifndef INSTALL_WINDOW_H_
#define INSTALL_WINDOW_H_

#include <string>
#include "fs/CFolderList.hpp"
#include "gui/MessageBox.h"
#include "ProgressWindow.h"

class MainWindow;

class InstallWindow : public GuiFrame, public CThread, public sigslot::has_slots<>
{
public:
	InstallWindow(CFolderList * list);
	~InstallWindow();
	
	void startInstalling()
	{
		resumeThread();
	}
	
	sigslot::signal1<GuiElement *> installWindowClosed;
	
private:
	void OnValidInstallClick(GuiElement * element, int val);
	void OnDestinationChoice(GuiElement * element, int choice);
	void OnCloseWindow(GuiElement * element, int val);
	void OnWindowClosed(GuiElement * element);
	void OnInstallProcessCancel(GuiElement *element, int val);
	
	void OnOpenEffectFinish(GuiElement *element);
	void OnCloseEffectFinish(GuiElement *element);
	
	void executeThread();
	void InstallProcess(int pos, int total);
	
	GuiFrame * drcFrame;
	
	CFolderList * folderList;
	
	MessageBox * messageBox;
	
	MainWindow * mainWindow;
	
	int folderCount;
	bool canceled;
	int target;

	//! 安装中：用户点「取消」或进度停滞触发 MCP_InstallTitleAbort
	volatile bool abortCurrentInstall;
	//! 某一项失败后：0 未选择，1 继续队列，2 结束
	volatile int failContinueChoice;
	//! 「已安装跳过」提示框点确定
	volatile int duplicateSkipAck;

	void OnAbortCurrentInstall(GuiElement * element, int val);
	void OnFailContinueYes(GuiElement * element, int val);
	void OnFailContinueNo(GuiElement * element, int val);
	void OnDuplicateSkipOk(GuiElement * element, int val);
	static void AppendInstallLog(const std::string & line);
	
	enum
	{
		NAND,
		USB
	};
	
};

#endif
