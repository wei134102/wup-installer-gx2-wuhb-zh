/****************************************************************************
 * Export installed game Title IDs (NAND + USB) to install/id_installed.txt
 ***************************************************************************/
#ifndef _IDINSTALLEDEXPORT_HPP_
#define _IDINSTALLEDEXPORT_HPP_

struct IdInstalledExportContext;

//! Stepped export (one MCP call per Step) so ProcUI/HOME can run between calls.
void IdInstalled_ExportContext_Create(IdInstalledExportContext **out);
void IdInstalled_ExportContext_Destroy(IdInstalledExportContext *ctx);
void IdInstalled_Export_Begin(IdInstalledExportContext *ctx);
void IdInstalled_Export_RequestCancel(IdInstalledExportContext *ctx);
bool IdInstalled_Export_IsActive(const IdInstalledExportContext *ctx);
//! Returns true when finished (check GetResult for success/failure).
bool IdInstalled_Export_Step(IdInstalledExportContext *ctx);
//! Result: count written, or -1 MCP, -2 file, -3 alloc, -4 cancelled.
int IdInstalled_Export_GetResult(IdInstalledExportContext *ctx, unsigned int *outNandCount, unsigned int *outUsbCount);

//! Blocking all-in-one (used by tests/tools only).
int IdInstalled_ExportToFile(unsigned int *outNandCount, unsigned int *outUsbCount);

#endif
