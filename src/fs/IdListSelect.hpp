/****************************************************************************
 * ID.txt selection helpers for WUP Installer GX2
 ***************************************************************************/
#ifndef _IDLISTSELECT_HPP_
#define _IDLISTSELECT_HPP_

#include "CFolderList.hpp"

//! Read install/ID.txt and select matching folders. Returns count selected, or -1 on missing file.
int CFolderList_SelectFromIdTxt(CFolderList *list);

#endif
