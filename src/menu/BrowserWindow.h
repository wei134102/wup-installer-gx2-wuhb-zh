/****************************************************************************
 * Copyright (C) 2015 Dimok
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
#ifndef _BROWSERWINDOW_H_
#define _BROWSERWINDOW_H_

#include "gui/Gui.h"
#include "gui/Scrollbar.h"
#include "fs/CFolderList.hpp"

struct IdInstalledExportContext;
class MessageBox;

class BrowserWindow : public GuiFrame, public sigslot::has_slots<>
{
public:
    BrowserWindow(int w, int h, CFolderList * folderList);
    virtual ~BrowserWindow();

	void update(GuiController *controller) override;
	//! Cancel in-progress export and close MCP before ProcUI releases GX2.
	void finishExportBeforeBackground(void);
	
	sigslot::signal1<GuiElement *> installButtonClicked;
	
private:
    int SearchSelectedButton();
    int SearchSelectedRightSideButton();
	
	void OnFolderButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	void OnDPADClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	void OnAButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	void OnPlusButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	void OnMinusButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	void OnInstallButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	void OnIdListButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	void OnExportInstalledIdClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	void AppendModalMessageBox(MessageBox *box);
	void CloseIdModalImmediate(void);
	void ShowExportResultMessage(int total, unsigned int nandCount, unsigned int usbCount);
	void PollExportJob(void);
	void OnModalMessageOverlayOpened(GuiElement *element);
	void OnIdTxtMessageBoxClick(GuiElement *element, int val);
	void OnIdTxtMessageBoxClosed(GuiElement *element);
	void SyncFolderButtonChecks();
	
	void OnScrollbarListChange(int selectItem, int pageIndex);
	
	Scrollbar scrollbar;
	
    GuiSound *buttonClickSound;
    
	GuiImageData *buttonImageData;
    GuiImageData *buttonCheckedImageData;
    GuiImageData *buttonHighlightedImageData;
	
    GuiImageData *selectImageData;
    GuiImageData *selectSelectedImageData;
    GuiImage selectImg;
    GuiImage unselectImg;
    GuiImage installImg;
    GuiImage idListBgImg;
    GuiImage exportIdBgImg;

	GuiImageData *plusImageData;
    GuiImageData *minusImageData;
    GuiImage plusImg;
    GuiImage minusImg;
	
	GuiText plusTxt;
	GuiText minusTxt;
	GuiText installTxt;
	GuiText idListTxt;
	GuiText exportIdTxt;
    
	GuiTrigger touchTrigger;
    GuiTrigger buttonATrigger;
    
    GuiTrigger buttonUpTrigger;
    GuiTrigger buttonDownTrigger;
    GuiTrigger buttonLeftTrigger;
    GuiTrigger buttonRightTrigger;
    
	GuiTrigger plusTrigger;
	GuiTrigger minusTrigger;
    
    GuiButton DPADButtons;
    GuiButton AButton;
	
	GuiButton plusButton;
	GuiButton minusButton;
	GuiButton installButton;
	GuiButton idListButton;
	GuiButton exportIdButton;
	
    GuiImage* plusButtonSelectedImage;
    GuiImage* minusButtonSelectedImage;
    GuiImage* installButtonSelectedImage;
    GuiImage* idListButtonSelectedImage;
    GuiImage* exportIdButtonSelectedImage;

    int pageIndex;
	int selectedItem;
	int buttonCount;
	
    bool rightSide = false;
    std::vector<GuiButton*> rightSideButtons;

    typedef struct
    {
        GuiImage *folderButtonImg;
        GuiImage *folderButtonCheckedImg;
        GuiImage *folderButtonHighlightedImg;
        GuiButton *folderButton;
        GuiText *folderButtonText;
        GuiText *folderButtonTextOver;
    } FolderButton;

    std::vector<FolderButton> folderButtons;
	
	CFolderList * folderList;

	GuiFrame *idMessageOverlay;
	MessageBox *idMessageBox;

	bool exportJobActive;
	IdInstalledExportContext *exportCtx;
	int exportJobResult;
	unsigned int exportJobNandCount;
	unsigned int exportJobUsbCount;
};

#endif //_BROSERWINDOW_H_
