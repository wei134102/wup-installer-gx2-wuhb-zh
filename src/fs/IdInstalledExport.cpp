/****************************************************************************
 * Export installed game Title IDs (NAND + USB) to install/id_installed.txt
 ***************************************************************************/
#include "IdInstalledExport.hpp"
#include "fs_utils.h"
#include "system/memory.h"
#include <coreinit/mcp.h>
#include <coreinit/memory.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <cstdio>
#include <cstring>
#include <set>
#include <unistd.h>

static const char * const kInstallDir = "fs:/vol/external01/install";
static const char * const kInstalledIdPath = "fs:/vol/external01/install/id_installed.txt";
static const char * const kInstalledLogPath = "fs:/vol/external01/install/id_installed_log.txt";
// MCP IOS IPC must use system/MEM1 memory — MEM2 (GX2 heap) causes MCP calls to hang.
static const uint32_t kMaxTitles = 128;
static const uint32_t kMaxMcpDevices = 4u;
static const uint32_t kMaxLogTitleLines = 80;

enum McpBufHeap
{
	MCP_BUF_NONE = 0,
	MCP_BUF_OS,
	MCP_BUF_MEM1,
	MCP_BUF_BUCKET,
};

static int gLogFd = -1;

static void LogClose()
{
	if(gLogFd >= 0)
	{
		::close(gLogFd);
		gLogFd = -1;
	}
}

static bool LogInit()
{
	LogClose();
	if(!CreateSubfolder(kInstallDir))
		return false;

	gLogFd = ::open(kInstalledLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if(gLogFd < 0)
		return false;

	return true;
}

static void LogLine(const char *fmt, ...)
{
	if(gLogFd < 0 || !fmt)
		return;

	char buf[768];
	va_list ap;
	va_start(ap, fmt);
	const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if(n <= 0)
		return;

	::write(gLogFd, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
}

static void LogFlush()
{
	if(gLogFd >= 0)
		::fsync(gLogFd);
}

struct McpWorkBuffer
{
	void *ptr;
	McpBufHeap heap;
};

static McpWorkBuffer McpBufAlloc(uint32_t bytes, const char *label)
{
	McpWorkBuffer buf = {nullptr, MCP_BUF_NONE};

	buf.ptr = OSAllocFromSystem(bytes, 0x40);
	if(buf.ptr)
		buf.heap = MCP_BUF_OS;
	else
	{
		buf.ptr = MEM1_alloc(bytes, 0x40);
		if(buf.ptr)
			buf.heap = MCP_BUF_MEM1;
		else
		{
			buf.ptr = MEMBucket_alloc(bytes, 0x40);
			if(buf.ptr)
				buf.heap = MCP_BUF_BUCKET;
		}
	}

	if(gLogFd >= 0)
	{
		if(buf.ptr)
		{
			const char *heapName = "OS";
			if(buf.heap == MCP_BUF_MEM1)
				heapName = "MEM1";
			else if(buf.heap == MCP_BUF_BUCKET)
				heapName = "bucket";
			LogLine("  MCP buffer %u bytes (%s) via %s\n", bytes, label, heapName);
		}
		else
			LogLine("  MCP buffer alloc FAILED %u bytes (%s)\n", bytes, label);
	}

	return buf;
}

static void McpBufFree(McpWorkBuffer *buf)
{
	if(!buf || !buf->ptr)
		return;

	switch(buf->heap)
	{
	case MCP_BUF_OS:
		OSFreeToSystem(buf->ptr);
		break;
	case MCP_BUF_MEM1:
		MEM1_free(buf->ptr);
		break;
	case MCP_BUF_BUCKET:
		MEMBucket_free(buf->ptr);
		break;
	default:
		break;
	}

	buf->ptr = nullptr;
	buf->heap = MCP_BUF_NONE;
}

static void LogTitleIdHex(uint64_t tid)
{
	LogLine("%016llX\n", (unsigned long long)tid);
}

static void LogTitleIdSet(const char *label, const std::set<uint64_t> &ids)
{
	LogLine("[%s] count=%u\n", label, (unsigned int)ids.size());
	uint32_t shown = 0;
	for(std::set<uint64_t>::const_iterator it = ids.begin(); it != ids.end(); ++it)
	{
		if(shown >= kMaxLogTitleLines)
		{
			LogLine("  ... (%u more not listed)\n", (unsigned int)ids.size() - shown);
			break;
		}
		LogLine("  ");
		LogTitleIdHex(*it);
		++shown;
	}
}

static size_t LogSetSizeDelta(const char *step, size_t before, size_t after)
{
	LogLine("[%s] ids: %u -> %u (+%d)\n", step, (unsigned int)before, (unsigned int)after, (int)after - (int)before);
	return after;
}

static bool IsBaseGameTitleId(uint64_t tid)
{
	return (tid >> 32) == 0x00050000ULL;
}

static void InsertTitleFromEntry(const MCPTitleListType &entry, std::set<uint64_t> &outIds)
{
	const uint64_t tid = entry.titleId;
	if(!IsBaseGameTitleId(tid))
		return;

	if(entry.appType == MCP_APP_TYPE_GAME || entry.appType == MCP_APP_TYPE_GAME_WII)
		outIds.insert(tid);
}

static void MergeMcpList(uint32_t count, MCPTitleListType *list, std::set<uint64_t> &outIds)
{
	for(uint32_t i = 0; i < count; ++i)
		InsertTitleFromEntry(list[i], outIds);
}

static void LogMcpTitleSamples(const char *apiName, uint32_t count, MCPTitleListType *list, int maxLines)
{
	LogLine("  %s: returned count=%u err=0\n", apiName, count);
	int shown = 0;
	for(uint32_t i = 0; i < count && shown < maxLines; ++i)
	{
		const MCPTitleListType &e = list[i];
		if(!IsBaseGameTitleId(e.titleId))
			continue;
		LogLine("    [%u] tid=%016llX appType=0x%08X path=%.56s dev=%.10s\n",
			i,
			(unsigned long long)e.titleId,
			(unsigned int)e.appType,
			e.path,
			e.indexedDevice);
		++shown;
	}
	if((uint32_t)shown < count)
		LogLine("    (only base-game lines shown, max %d)\n", maxLines);
}

static bool QueryMcpGamesForDevice(int mcpHandle, MCPDeviceType deviceType, const char *label,
	McpWorkBuffer *titleBuf, std::set<uint64_t> &outIds)
{
	const size_t before = outIds.size();
	LogLine("\n--- MCP games: %s (deviceType=%d) ---\n", label, (int)deviceType);

	if(!titleBuf || !titleBuf->ptr)
	{
		LogLine("  skipped (no title buffer)\n");
		return false;
	}

	const uint32_t bytes = kMaxTitles * (uint32_t)sizeof(MCPTitleListType);
	MCPTitleListType *list = (MCPTitleListType *)titleBuf->ptr;

	uint32_t count = kMaxTitles;
	memset(list, 0, bytes);

	LogLine("  calling MCP_TitleListByAppAndDeviceType(GAME)...\n");
	LogFlush();

	const MCPError err = MCP_TitleListByAppAndDeviceType(
		mcpHandle, MCP_APP_TYPE_GAME, deviceType, &count, list, bytes);

	LogLine("  MCP_TitleListByAppAndDeviceType: err=%d count=%u\n", (int)err, count);
	LogFlush();

	if(err != 0)
	{
		LogSetSizeDelta(label, before, outIds.size());
		return false;
	}

	if(count > kMaxTitles)
	{
		LogLine("  WARNING: MCP returned %u titles, truncating to %u\n", count, kMaxTitles);
		count = kMaxTitles;
	}

	if(count > 0)
	{
		LogMcpTitleSamples("TitleListByAppAndDeviceType", count, list, 8);
		MergeMcpList(count, list, outIds);
	}

	LogSetSizeDelta(label, before, outIds.size());
	return count > 0;
}

static void CollectMcpWiiGames(int mcpHandle, MCPDeviceType deviceType, const char *label,
	McpWorkBuffer *titleBuf, std::set<uint64_t> &outIds)
{
	if(!titleBuf || !titleBuf->ptr)
		return;

	const size_t before = outIds.size();
	const uint32_t bytes = kMaxTitles * (uint32_t)sizeof(MCPTitleListType);
	MCPTitleListType *list = (MCPTitleListType *)titleBuf->ptr;

	uint32_t count = kMaxTitles;
	memset(list, 0, bytes);

	const MCPError err = MCP_TitleListByAppAndDeviceType(
		mcpHandle, MCP_APP_TYPE_GAME_WII, deviceType, &count, list, bytes);

	LogLine("  MCP_TitleListByAppAndDeviceType(WII): err=%d count=%u\n", (int)err, count);
	LogFlush();

	if(err == 0 && count > 0)
	{
		if(count > kMaxTitles)
			count = kMaxTitles;
		MergeMcpList(count, list, outIds);
	}

	if(outIds.size() != before)
		LogSetSizeDelta(label, before, outIds.size());
}

static void LogMcpFullDeviceList(int mcpHandle, McpWorkBuffer *devBuf)
{
	LogLine("\n--- MCP_FullDeviceList ---\n");

	if(!devBuf || !devBuf->ptr)
	{
		LogLine("  skipped (no device buffer)\n");
		return;
	}

	const uint32_t listBytes = kMaxMcpDevices * (uint32_t)sizeof(MCPDevice);
	MCPDevice *devs = (MCPDevice *)devBuf->ptr;

	memset(devs, 0, listBytes);
	int num = 0;

	LogLine("  calling MCP_FullDeviceList...\n");
	LogFlush();

	const MCPError err = MCP_FullDeviceList(mcpHandle, &num, devs, listBytes);
	LogLine("  err=%d numDevices=%d\n", (int)err, num);
	LogFlush();

	if(err == 0 && num > 0)
	{
		const int n = num > (int)kMaxMcpDevices ? (int)kMaxMcpDevices : num;
		for(int i = 0; i < n; ++i)
		{
			LogLine("  [%d] type=\"%.8s\" fs=\"%.8s\" path=\"%s\"\n",
				i,
				devs[i].type,
				devs[i].filesystem,
				devs[i].path);
		}
	}
}

static bool WriteTitleIdFile(const std::set<uint64_t> &ids)
{
	if(!CreateSubfolder(kInstallDir))
	{
		LogLine("WriteTitleIdFile: CreateSubfolder failed\n");
		return false;
	}

	const int fd = ::open(kInstalledIdPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if(fd < 0)
	{
		LogLine("WriteTitleIdFile: open %s failed errno=%d\n", kInstalledIdPath, (int)errno);
		return false;
	}

	uint32_t lines = 0;
	for(std::set<uint64_t>::const_iterator it = ids.begin(); it != ids.end(); ++it)
	{
		char line[24];
		std::snprintf(line, sizeof(line), "%016llX\n", (unsigned long long)*it);
		const size_t len = std::strlen(line);
		ssize_t written = ::write(fd, line, len);
		if(written != (ssize_t)len)
		{
			LogLine("WriteTitleIdFile: write failed at line %u errno=%d\n", lines, (int)errno);
			::close(fd);
			return false;
		}
		++lines;
	}

	::close(fd);
	LogLine("WriteTitleIdFile: wrote %u lines to %s\n", lines, kInstalledIdPath);
	return true;
}

enum ExportPhase
{
	Phase_Idle = 0,
	Phase_OpenMcp,
	Phase_Alloc,
	Phase_DevList,
	Phase_MlcGames,
	Phase_MlcWii,
	Phase_UsbGames,
	Phase_UsbWii,
	Phase_CloseMcp,
	Phase_WriteFile,
	Phase_Done,
};

struct IdInstalledExportContext
{
	ExportPhase phase;
	bool active;
	bool cancel;
	bool finished;
	int result;
	unsigned int nandCount;
	unsigned int usbCount;
	bool logOk;
	int mcpHandle;
	McpWorkBuffer titleBuf;
	McpWorkBuffer devBuf;
	std::set<uint64_t> nandIds;
	std::set<uint64_t> usbIds;
};

static void ExportFail(IdInstalledExportContext *ctx, int code)
{
	if(!ctx)
		return;

	ctx->result = code;
	ctx->phase = Phase_CloseMcp;
}

static void ExportCloseMcp(IdInstalledExportContext *ctx)
{
	if(!ctx)
		return;

	McpBufFree(&ctx->titleBuf);
	McpBufFree(&ctx->devBuf);

	if(ctx->mcpHandle != 0)
	{
		MCP_Close(ctx->mcpHandle);
		ctx->mcpHandle = 0;
		if(ctx->logOk)
			LogLine("\nMCP_Close done\n");
	}
}

static void ExportWriteAndFinish(IdInstalledExportContext *ctx)
{
	if(!ctx)
		return;

	ctx->nandCount = (unsigned int)ctx->nandIds.size();
	ctx->usbCount = (unsigned int)ctx->usbIds.size();

	std::set<uint64_t> allIds;
	for(std::set<uint64_t>::const_iterator it = ctx->nandIds.begin(); it != ctx->nandIds.end(); ++it)
		allIds.insert(*it);
	for(std::set<uint64_t>::const_iterator it = ctx->usbIds.begin(); it != ctx->usbIds.end(); ++it)
		allIds.insert(*it);

	if(ctx->logOk)
	{
		LogLine("\n========== Final sets ==========\n");
		LogTitleIdSet("NAND (mlc)", ctx->nandIds);
		LogTitleIdSet("USB", ctx->usbIds);
		LogTitleIdSet("Merged export", allIds);
	}

	const bool wrote = WriteTitleIdFile(allIds);
	if(ctx->logOk)
	{
		if(ctx->cancel)
			LogLine("\nRESULT: cancelled by user\n");
		else
			LogLine("\nRESULT: export %s, total=%u (nand=%u usb=%u)\n",
				wrote ? "OK" : "FAILED",
				(unsigned int)allIds.size(),
				ctx->nandCount,
				ctx->usbCount);
		LogClose();
	}

	ctx->finished = true;
	ctx->active = false;
	ctx->phase = Phase_Done;

	if(ctx->cancel)
		ctx->result = -4;
	else if(!wrote)
		ctx->result = -2;
	else
		ctx->result = (int)allIds.size();
}

void IdInstalled_ExportContext_Create(IdInstalledExportContext **out)
{
	if(out)
		*out = new IdInstalledExportContext();
}

void IdInstalled_ExportContext_Destroy(IdInstalledExportContext *ctx)
{
	if(!ctx)
		return;

	ExportCloseMcp(ctx);
	LogClose();
	delete ctx;
}

void IdInstalled_Export_Begin(IdInstalledExportContext *ctx)
{
	if(!ctx)
		return;

	ctx->phase = Phase_OpenMcp;
	ctx->active = true;
	ctx->cancel = false;
	ctx->finished = false;
	ctx->result = 0;
	ctx->nandCount = 0;
	ctx->usbCount = 0;
	ctx->mcpHandle = 0;
	ctx->titleBuf = {nullptr, MCP_BUF_NONE};
	ctx->devBuf = {nullptr, MCP_BUF_NONE};
	ctx->nandIds.clear();
	ctx->usbIds.clear();

	ctx->logOk = LogInit();
	if(ctx->logOk)
	{
		LogLine("IdInstalled export log\n");
		LogLine("output: %s\n", kInstalledIdPath);
		LogLine("log:    %s\n", kInstalledLogPath);
		LogLine("expected game path pattern:\n");
		LogLine("  <storage>/usr/title/00050000/<8 hex>/\n");
		LogLine("  e.g. fs:/vol/storage_usb01/usr/title/00050000/10112300\n");
		LogLine("MCP title buffer: max=%u x %u bytes = %u (OS/MEM1, not MEM2)\n",
			kMaxTitles,
			(unsigned int)sizeof(MCPTitleListType),
			kMaxTitles * (uint32_t)sizeof(MCPTitleListType));
	}
}

void IdInstalled_Export_RequestCancel(IdInstalledExportContext *ctx)
{
	if(ctx && ctx->active && !ctx->finished)
		ctx->cancel = true;
}

bool IdInstalled_Export_IsActive(const IdInstalledExportContext *ctx)
{
	return ctx && ctx->active && !ctx->finished;
}

bool IdInstalled_Export_Step(IdInstalledExportContext *ctx)
{
	if(!ctx || !ctx->active || ctx->finished)
		return true;

	if(ctx->cancel && ctx->phase != Phase_CloseMcp && ctx->phase != Phase_WriteFile && ctx->phase != Phase_Done)
	{
		ctx->phase = Phase_CloseMcp;
		if(ctx->logOk)
			LogLine("\nExport cancelled (HOME / background)\n");
	}

	switch(ctx->phase)
	{
	case Phase_OpenMcp:
	{
		ctx->mcpHandle = (int)MCP_Open();
		if(ctx->logOk)
		{
			LogLine("\nMCP_Open -> handle=%u (0 = failure)\n", (unsigned int)ctx->mcpHandle);
			LogFlush();
		}
		if(ctx->mcpHandle == 0)
		{
			if(ctx->logOk)
				LogLine("\nRESULT: aborted, MCP_Open failed\n");
			LogClose();
			ctx->finished = true;
			ctx->active = false;
			ctx->result = -1;
			ctx->phase = Phase_Done;
			return true;
		}
		ctx->phase = Phase_Alloc;
		return false;
	}
	case Phase_Alloc:
	{
		const uint32_t titleBytes = kMaxTitles * (uint32_t)sizeof(MCPTitleListType);
		const uint32_t devBytes = kMaxMcpDevices * (uint32_t)sizeof(MCPDevice);
		ctx->titleBuf = McpBufAlloc(titleBytes, "title list");
		ctx->devBuf = McpBufAlloc(devBytes, "device list");
		if(!ctx->titleBuf.ptr)
		{
			if(ctx->logOk)
				LogLine("\nRESULT: aborted, cannot allocate MCP title buffer\n");
			ExportFail(ctx, -3);
			return false;
		}
		ctx->phase = Phase_DevList;
		return false;
	}
	case Phase_DevList:
		if(ctx->logOk)
			LogMcpFullDeviceList(ctx->mcpHandle, &ctx->devBuf);
		ctx->phase = Phase_MlcGames;
		return false;
	case Phase_MlcGames:
		QueryMcpGamesForDevice(ctx->mcpHandle, MCP_DEVICE_TYPE_MLC, "MLC", &ctx->titleBuf, ctx->nandIds);
		ctx->phase = Phase_MlcWii;
		return false;
	case Phase_MlcWii:
		CollectMcpWiiGames(ctx->mcpHandle, MCP_DEVICE_TYPE_MLC, "MLC Wii", &ctx->titleBuf, ctx->nandIds);
		ctx->phase = Phase_UsbGames;
		return false;
	case Phase_UsbGames:
		QueryMcpGamesForDevice(ctx->mcpHandle, MCP_DEVICE_TYPE_USB, "USB", &ctx->titleBuf, ctx->usbIds);
		ctx->phase = Phase_UsbWii;
		return false;
	case Phase_UsbWii:
		CollectMcpWiiGames(ctx->mcpHandle, MCP_DEVICE_TYPE_USB, "USB Wii", &ctx->titleBuf, ctx->usbIds);
		if(ctx->logOk)
			LogLine("\n========== FS scan skipped (WUHB: opendir on MLC/USB may hang) ==========\n");
		ctx->phase = Phase_CloseMcp;
		return false;
	case Phase_CloseMcp:
		ExportCloseMcp(ctx);
		if(ctx->cancel || ctx->result < 0)
		{
			if(ctx->result == 0 && ctx->cancel)
				ctx->result = -4;
			if(ctx->logOk && ctx->result == -3)
				LogLine("\nRESULT: aborted, cannot allocate MCP title buffer\n");
			if(ctx->logOk)
				LogClose();
			ctx->finished = true;
			ctx->active = false;
			ctx->phase = Phase_Done;
			return true;
		}
		ctx->phase = Phase_WriteFile;
		return false;
	case Phase_WriteFile:
		ExportWriteAndFinish(ctx);
		return true;
	default:
		ctx->finished = true;
		ctx->active = false;
		return true;
	}
}

int IdInstalled_Export_GetResult(IdInstalledExportContext *ctx, unsigned int *outNandCount, unsigned int *outUsbCount)
{
	if(outNandCount)
		*outNandCount = ctx ? ctx->nandCount : 0;
	if(outUsbCount)
		*outUsbCount = ctx ? ctx->usbCount : 0;
	return ctx ? ctx->result : -1;
}

int IdInstalled_ExportToFile(unsigned int *outNandCount, unsigned int *outUsbCount)
{
	IdInstalledExportContext *ctx = nullptr;
	IdInstalled_ExportContext_Create(&ctx);
	IdInstalled_Export_Begin(ctx);
	while(!IdInstalled_Export_Step(ctx))
		;
	const int result = IdInstalled_Export_GetResult(ctx, outNandCount, outUsbCount);
	IdInstalled_ExportContext_Destroy(ctx);
	return result;
}
