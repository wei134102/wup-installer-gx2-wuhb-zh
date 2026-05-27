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
#include "BrowserWindow.h"
#include "Application.h"
#include "fs/IdListSelect.hpp"
#include "fs/IdInstalledExport.hpp"
#include "gui/MessageBox.h"
#include "menu/MainWindow.h"
#include "utils/StringTools.h"

#define MAX_FOLDERS_PER_PAGE 3

BrowserWindow::BrowserWindow(int w, int h, CFolderList * list)
    : GuiFrame(w, h)
	, scrollbar(h - 150)
    , buttonClickSound(Resources::GetSound("button_click.mp3"))
    , buttonImageData(Resources::GetImageData("choiceUncheckedRectangle.png"))
    , buttonCheckedImageData(Resources::GetImageData("choiceCheckedRectangle.png"))
    , buttonHighlightedImageData(Resources::GetImageData("choiceSelectedRectangle.png"))
    , selectImageData(Resources::GetImageData("select_button.png"))
	, selectSelectedImageData(Resources::GetImageData("select_buttonSelected.png"))
	, selectImg(selectImageData)
	, unselectImg(selectImageData)
	, installImg(selectImageData)
	, idListBgImg(selectImageData)
	, exportIdBgImg(selectImageData)
    , plusImageData(Resources::GetImageData("plus.png"))
    , minusImageData(Resources::GetImageData("minus.png"))
	, plusImg(plusImageData)
	, minusImg(minusImageData)
	, plusTxt("选择全部", 42, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f))
	, minusTxt("取消全选", 42, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f))
	, installTxt("安装", 42, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f))
	, idListTxt("ID安装", 42, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f))
	, exportIdTxt("提取ID", 42, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f))
    , touchTrigger(GuiTrigger::CHANNEL_1, GuiTrigger::VPAD_TOUCH)
    , buttonATrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_A, true)
    , buttonUpTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_UP | GuiTrigger::STICK_L_UP, true)
    , buttonDownTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_DOWN | GuiTrigger::STICK_L_DOWN, true)
	, buttonLeftTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_LEFT | GuiTrigger::STICK_L_LEFT, true)
    , buttonRightTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_RIGHT | GuiTrigger::STICK_L_RIGHT, true)
    , plusTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_PLUS, true)
    , minusTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_MINUS, true)
    , DPADButtons(w,h)
    , AButton(w,h)
	, plusButton(selectImg.getWidth(), selectImg.getHeight())
	, minusButton(selectImg.getWidth(), selectImg.getHeight())
	, installButton(selectImg.getWidth(), selectImg.getHeight())
	, idListButton(selectImg.getWidth(), selectImg.getHeight())
	, exportIdButton(selectImg.getWidth(), selectImg.getHeight())
	, idMessageOverlay(nullptr)
	, idMessageBox(nullptr)
	, exportJobActive(false)
	, exportCtx(nullptr)
	, exportJobResult(0)
	, exportJobNandCount(0)
	, exportJobUsbCount(0)
{
	folderList = list;
	pageIndex = 0;
	selectedItem = -1;
	
    buttonCount = folderList->GetCount();
	folderButtons.resize(buttonCount);
	
	for(int i = 0; i < buttonCount; i++)
	{      
		folderButtons[i].folderButtonImg = new GuiImage(buttonImageData);
		folderButtons[i].folderButtonCheckedImg = new GuiImage(buttonCheckedImageData);
		folderButtons[i].folderButtonHighlightedImg = new GuiImage(buttonHighlightedImageData);
		folderButtons[i].folderButton = new GuiButton(folderButtons[i].folderButtonImg->getWidth(), folderButtons[i].folderButtonImg->getHeight());
		
		folderButtons[i].folderButtonText = new GuiText(folderList->GetName(i).c_str(), 42, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
		folderButtons[i].folderButtonText->setMaxWidth(folderButtons[i].folderButtonImg->getWidth() - 70, GuiText::DOTTED);
		folderButtons[i].folderButtonText->setPosition(35, 0);
		
		folderButtons[i].folderButtonTextOver = new GuiText(folderList->GetName(i).c_str(), 42, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
		folderButtons[i].folderButtonTextOver->setMaxWidth(folderButtons[i].folderButtonImg->getWidth() - 94, GuiText::SCROLL_HORIZONTAL);
		folderButtons[i].folderButtonTextOver->setPosition(35, 0);
		
		folderButtons[i].folderButton->setImageSelectOver(folderButtons[i].folderButtonHighlightedImg);
		folderButtons[i].folderButton->setLabel(folderButtons[i].folderButtonText);
		folderButtons[i].folderButton->setLabelOver(folderButtons[i].folderButtonTextOver);
		folderButtons[i].folderButton->setSoundClick(buttonClickSound);
		folderButtons[i].folderButton->setImage(folderButtons[i].folderButtonImg);
		folderButtons[i].folderButton->setImageChecked(folderButtons[i].folderButtonCheckedImg);
		if(folderList->IsSelected(i))
			folderButtons[i].folderButton->check();
		
		folderButtons[i].folderButton->setPosition(0, 150 - (folderButtons[i].folderButtonImg->getHeight() + 30) * i);
		folderButtons[i].folderButton->setAlignment(ALIGN_LEFT | ALIGN_MIDDLE);
        folderButtons[i].folderButton->setTrigger(&touchTrigger);
		folderButtons[i].folderButton->clicked.connect(this, &BrowserWindow::OnFolderButtonClick);
		
		this->append(folderButtons[i].folderButton);
	}
	
	if(buttonCount > MAX_FOLDERS_PER_PAGE)
    {
		scrollbar.SetPageSize(MAX_FOLDERS_PER_PAGE);
        scrollbar.SetEntrieCount(buttonCount);
        scrollbar.setAlignment(ALIGN_RIGHT | ALIGN_MIDDLE);
        scrollbar.setPosition(0, -30);
		scrollbar.SetSelected(0, 0);
        scrollbar.listChanged.connect(this, &BrowserWindow::OnScrollbarListChange);
        this->append(&scrollbar);
    }
	
	DPADButtons.setTrigger(&buttonUpTrigger);
    DPADButtons.setTrigger(&buttonDownTrigger);
	DPADButtons.setTrigger(&buttonLeftTrigger);
    DPADButtons.setTrigger(&buttonRightTrigger);
    DPADButtons.clicked.connect(this, &BrowserWindow::OnDPADClick);
	this->append(&DPADButtons);
    
	AButton.setTrigger(&buttonATrigger);
    AButton.clicked.connect(this, &BrowserWindow::OnAButtonClick);
	this->append(&AButton);

	const int btnW = selectImg.getWidth();
	const int btnH = selectImg.getHeight();
	const int kRightBtnCount = 5;
	/* 原 4 个按钮占 3 个间距；5 个按钮在同一高度内均分 */
	const int btnStep = ((btnH + 8) * (kRightBtnCount - 2)) / (kRightBtnCount - 1);
	const int btnX = 240;
	const int btnY0 = -95;

	plusImg.setAlignment(ALIGN_BOTTOM | ALIGN_RIGHT);
	plusImg.setPosition(-10, 10);
    plusTxt.setMaxWidth(btnW - 5, GuiText::WRAP);
    plusButton.setLabel(&plusTxt);
    plusButton.setImage(&selectImg);
	plusButton.setIcon(&plusImg);
    plusButton.setAlignment(ALIGN_TOP | ALIGN_RIGHT);
    plusButton.setPosition(btnX, btnY0);
    plusButton.clicked.connect(this, &BrowserWindow::OnPlusButtonClick);
    plusButton.setTrigger(&plusTrigger);
    plusButton.setTrigger(&touchTrigger);
    plusButton.setSoundClick(buttonClickSound);
    plusButton.setEffectGrow();
	plusButton.setSelectable(true);
	plusButtonSelectedImage = new GuiImage(selectSelectedImageData);
	plusButton.setImageSelectOver(plusButtonSelectedImage);
    this->append(&plusButton);
	rightSideButtons.push_back(&plusButton);

    minusImg.setAlignment(ALIGN_BOTTOM | ALIGN_RIGHT);
	minusImg.setPosition(-10, 10);
	minusTxt.setMaxWidth(btnW - 5, GuiText::WRAP);
    minusButton.setLabel(&minusTxt);
    minusButton.setImage(&unselectImg);
	minusButton.setIcon(&minusImg);
    minusButton.setAlignment(ALIGN_TOP | ALIGN_RIGHT);
    minusButton.setPosition(btnX, btnY0 - btnStep);
    minusButton.clicked.connect(this, &BrowserWindow::OnMinusButtonClick);
    minusButton.setTrigger(&minusTrigger);
    minusButton.setTrigger(&touchTrigger);
    minusButton.setSoundClick(buttonClickSound);
    minusButton.setEffectGrow();
	minusButton.setSelectable(true);
	minusButtonSelectedImage = new GuiImage(selectSelectedImageData);
	minusButton.setImageSelectOver(minusButtonSelectedImage);
    this->append(&minusButton);
	rightSideButtons.push_back(&minusButton);
	
	installTxt.setMaxWidth(btnW - 5, GuiText::WRAP);
    installButton.setLabel(&installTxt);
    installButton.setImage(&installImg);
	installButton.setAlignment(ALIGN_TOP | ALIGN_RIGHT);
    installButton.setPosition(btnX, btnY0 - btnStep * 2);
    installButton.clicked.connect(this, &BrowserWindow::OnInstallButtonClick);
    installButton.setTrigger(&touchTrigger);
    installButton.setSoundClick(buttonClickSound);
    installButton.setEffectGrow();
	installButton.setSelectable(true);
	installButtonSelectedImage = new GuiImage(selectSelectedImageData);
	installButton.setImageSelectOver(installButtonSelectedImage);
    this->append(&installButton);
	rightSideButtons.push_back(&installButton);

	idListTxt.setMaxWidth(btnW - 5, GuiText::WRAP);
	idListButton.setLabel(&idListTxt);
	idListButton.setImage(&idListBgImg);
	idListButton.setAlignment(ALIGN_TOP | ALIGN_RIGHT);
	idListButton.setPosition(btnX, btnY0 - btnStep * 3);
	idListButton.clicked.connect(this, &BrowserWindow::OnIdListButtonClick);
	idListButton.setTrigger(&touchTrigger);
	idListButton.setSoundClick(buttonClickSound);
	idListButton.setEffectGrow();
	idListButton.setSelectable(true);
	idListButtonSelectedImage = new GuiImage(selectSelectedImageData);
	idListButton.setImageSelectOver(idListButtonSelectedImage);
	this->append(&idListButton);
	rightSideButtons.push_back(&idListButton);

	exportIdTxt.setMaxWidth(btnW - 5, GuiText::WRAP);
	exportIdButton.setLabel(&exportIdTxt);
	exportIdButton.setImage(&exportIdBgImg);
	exportIdButton.setAlignment(ALIGN_TOP | ALIGN_RIGHT);
	exportIdButton.setPosition(btnX, btnY0 - btnStep * 4);
	exportIdButton.clicked.connect(this, &BrowserWindow::OnExportInstalledIdClick);
	exportIdButton.setTrigger(&touchTrigger);
	exportIdButton.setSoundClick(buttonClickSound);
	exportIdButton.setEffectGrow();
	exportIdButton.setSelectable(true);
	exportIdButtonSelectedImage = new GuiImage(selectSelectedImageData);
	exportIdButton.setImageSelectOver(exportIdButtonSelectedImage);
	this->append(&exportIdButton);
	rightSideButtons.push_back(&exportIdButton);
}

void BrowserWindow::SyncFolderButtonChecks()
{
	for(int i = 0; i < buttonCount; i++)
		folderButtons[i].folderButton->setChecked(folderList->IsSelected(i));
}

BrowserWindow::~BrowserWindow()
{
	finishExportBeforeBackground();

	CloseIdModalImmediate();

    for(u32 i = 0; i < folderButtons.size(); ++i)
    {
        delete folderButtons[i].folderButtonImg;
        delete folderButtons[i].folderButtonCheckedImg;
        delete folderButtons[i].folderButtonHighlightedImg;
        delete folderButtons[i].folderButton;
        delete folderButtons[i].folderButtonText;
        delete folderButtons[i].folderButtonTextOver;
    }
	
	folderButtons.clear();
   
	delete plusButtonSelectedImage;
	delete minusButtonSelectedImage;
	delete installButtonSelectedImage;
	delete idListButtonSelectedImage;
	delete exportIdButtonSelectedImage;

    Resources::RemoveImageData(buttonImageData);
    Resources::RemoveImageData(buttonCheckedImageData);
    Resources::RemoveImageData(buttonHighlightedImageData);
    Resources::RemoveImageData(selectImageData);
	Resources::RemoveImageData(selectSelectedImageData);
    Resources::RemoveImageData(plusImageData);
    Resources::RemoveImageData(minusImageData);
    Resources::RemoveSound(buttonClickSound);
}

int BrowserWindow::SearchSelectedButton()
{
	int index = -1;
	for(int i = 0; i < buttonCount && index < 0; i++)
	{
		if(folderButtons[i].folderButton->getState() == STATE_SELECTED)
			index = i;
	}
	
	return index;
}

int BrowserWindow::SearchSelectedRightSideButton()
{
	int index = -1;
	for(int i = 0; i < (int)rightSideButtons.size() && index < 0; i++)
	{
		if(rightSideButtons[i]->getState() == STATE_SELECTED)
			index = i;
	}
	
	return index;
}

void BrowserWindow::OnFolderButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
{	
	button->check();
	
	for(int i = 0; i < buttonCount; i++)
	{
		if(folderButtons[i].folderButton == button)
		{
			folderList->Click(i);
			folderButtons[i].folderButton->setState(STATE_SELECTED);
			
			selectedItem = i - pageIndex;
			scrollbar.SetSelectedItem(selectedItem);
		}
		else
			folderButtons[i].folderButton->clearState(STATE_SELECTED);
	}
}

void BrowserWindow::OnAButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
{
	if (rightSide)
	{
		int index = SearchSelectedRightSideButton();

		if (index < 0)
			return;

		rightSideButtons[index]->clicked(rightSideButtons[index], controller, trigger);
	}
	else
	{	
		int index = SearchSelectedButton();
		
		if(index < 0)
			return;
		
		folderList->Click(index);
		folderButtons[index].folderButton->check();
	}
}

void BrowserWindow::OnDPADClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
{
	if (trigger == &buttonLeftTrigger || trigger == &buttonRightTrigger)
	{
		rightSide = !rightSide;
		if (rightSide)
		{
			int lindex = SearchSelectedButton();
			if (lindex >= 0)
				folderButtons[lindex].folderButton->clearState(STATE_SELECTED);

			int index = SearchSelectedRightSideButton();

			if (index >= 0)
				rightSideButtons[index]->clearState(STATE_SELECTED);
			index = 0;
			rightSideButtons[index]->setState(STATE_SELECTED);
		}
		else
		{
			int rindex = SearchSelectedRightSideButton();
			if (rindex >= 0)
				rightSideButtons[rindex]->clearState(STATE_SELECTED);

			int index = SearchSelectedButton();
			
			if (index >= 0)
				folderButtons[index].folderButton->clearState(STATE_SELECTED);
			index = 0;
			folderButtons[index].folderButton->setState(STATE_SELECTED);

			pageIndex = 0;
			selectedItem = 0;

			scrollbar.SetSelected(selectedItem, pageIndex);
		}
		
	}

	if (rightSide)
	{
		int index = SearchSelectedRightSideButton();

		if(index < 0 && trigger == &buttonUpTrigger)
			return;

		if(trigger == &buttonUpTrigger && index > 0)
		{
			rightSideButtons[index]->clearState(STATE_SELECTED);
			index--;
			rightSideButtons[index]->setState(STATE_SELECTED);
		}
		else if(trigger == &buttonDownTrigger && index < (int)rightSideButtons.size() - 1)
		{
			if(index >= 0)
				rightSideButtons[index]->clearState(STATE_SELECTED);
			index++;
			rightSideButtons[index]->setState(STATE_SELECTED);
		}
	}
	else
	{
		int index = SearchSelectedButton();
		
		if(index < 0 && trigger == &buttonUpTrigger)
			return;
		
		if(trigger == &buttonUpTrigger && index > 0)
		{
			folderButtons[index].folderButton->clearState(STATE_SELECTED);
			index--;
			folderButtons[index].folderButton->setState(STATE_SELECTED);
			
			if(selectedItem == 0 && pageIndex > 0)
				--pageIndex;
			else if(pageIndex+selectedItem > 0)
				--selectedItem;
		}
		else if(trigger == &buttonDownTrigger && index < buttonCount-1)
		{
			if(index >= 0)
				folderButtons[index].folderButton->clearState(STATE_SELECTED);
			index++;
			folderButtons[index].folderButton->setState(STATE_SELECTED);
			
			if(pageIndex+selectedItem + 1 < buttonCount)
			{
				if(selectedItem == MAX_FOLDERS_PER_PAGE-1)
					pageIndex++;
				else
					selectedItem++;
			}
		}
		
		scrollbar.SetSelected(selectedItem, pageIndex);
	}
}
	
void BrowserWindow::OnPlusButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
{
	for(int i = 0; i < buttonCount; i++)
	{
		if(!folderList->IsSelected(i))
		{
			folderList->Select(i);
			folderButtons[i].folderButton->check();
		}
	}
}

void BrowserWindow::OnMinusButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
{
	for(int i = 0; i < buttonCount; i++)
	{
		if(folderList->IsSelected(i))
		{
			folderList->UnSelect(i);
			folderButtons[i].folderButton->check();
		}
	}
}

void BrowserWindow::OnInstallButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
{
	installButtonClicked(this);
}

void BrowserWindow::OnIdListButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
{
	folderList->UnSelectAll();
	int matched = CFolderList_SelectFromIdTxt(folderList);
	SyncFolderButtonChecks();

	if(matched < 0)
	{
		MessageBox *box = new MessageBox(MessageBox::BT_OK, MessageBox::IT_ICONERROR, false);
		box->setTitle("ID.txt");
		box->setMessage1("未找到 install/ID.txt");
		box->setMessage2("请将 U-Wii-X 导出的 ID.txt 放到 SD 卡 install 目录");
		box->messageOkClicked.connect(this, &BrowserWindow::OnIdTxtMessageBoxClick);
		AppendModalMessageBox(box);
		return;
	}

	if(matched == 0)
	{
		MessageBox *box = new MessageBox(MessageBox::BT_OK, MessageBox::IT_ICONEXCLAMATION, false);
		box->setTitle("ID.txt");
		box->setMessage1("ID.txt 中没有匹配的安装包");
		box->setMessage2("请确认 Title ID 与文件夹名或 title.tmd 一致");
		box->messageOkClicked.connect(this, &BrowserWindow::OnIdTxtMessageBoxClick);
		AppendModalMessageBox(box);
		return;
	}

	installButtonClicked(this);
}

void BrowserWindow::update(GuiController *controller)
{
	PollExportJob();
	GuiFrame::update(controller);
}

void BrowserWindow::finishExportBeforeBackground(void)
{
	if(!exportJobActive || !exportCtx)
		return;

	IdInstalled_Export_RequestCancel(exportCtx);
	while(!IdInstalled_Export_Step(exportCtx))
		;

	IdInstalled_ExportContext_Destroy(exportCtx);
	exportCtx = nullptr;
	exportJobActive = false;
	CloseIdModalImmediate();
}

void BrowserWindow::PollExportJob(void)
{
	if(!exportJobActive || !exportCtx)
		return;

	if(!IdInstalled_Export_Step(exportCtx))
		return;

	exportJobResult = IdInstalled_Export_GetResult(exportCtx, &exportJobNandCount, &exportJobUsbCount);
	IdInstalled_ExportContext_Destroy(exportCtx);
	exportCtx = nullptr;
	exportJobActive = false;

	CloseIdModalImmediate();
	ShowExportResultMessage(exportJobResult, exportJobNandCount, exportJobUsbCount);
}

void BrowserWindow::ShowExportResultMessage(int total, unsigned int nandCount, unsigned int usbCount)
{
	MessageBox *box = nullptr;
	if(total < 0)
	{
		box = new MessageBox(MessageBox::BT_OK, MessageBox::IT_ICONERROR, false);
		box->setTitle("id_installed.txt");
		if(total == -1)
		{
			box->setMessage1("无法打开 MCP");
			box->setMessage2("请稍后重试\n已写 install/id_installed_log.txt");
		}
		else if(total == -3)
		{
			box->setMessage1("内存不足，无法查询已安装游戏");
			box->setMessage2("请关闭其它应用后重试\n已写 install/id_installed_log.txt");
		}
		else
		{
			box->setMessage1("无法写入 install/id_installed.txt");
			box->setMessage2("请确认 SD 卡已挂载且可写\n已写 install/id_installed_log.txt");
		}
	}
	else
	{
		box = new MessageBox(MessageBox::BT_OK, MessageBox::IT_ICONINFORMATION, false);
		box->setTitle("id_installed.txt");
		box->setMessage1(fmt("已导出 %d 个游戏 ID", total));
		box->setMessage2(fmt("主机 %u 个，USB %u 个\n详见 install/id_installed_log.txt", nandCount, usbCount));
	}

	box->messageOkClicked.connect(this, &BrowserWindow::OnIdTxtMessageBoxClick);
	AppendModalMessageBox(box);
}

void BrowserWindow::OnExportInstalledIdClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
{
	(void)button;
	(void)controller;
	(void)trigger;

	if(exportJobActive)
		return;

	exportJobActive = true;
	exportJobResult = 0;
	exportJobNandCount = 0;
	exportJobUsbCount = 0;
	exportCtx = nullptr;

	IdInstalled_ExportContext_Create(&exportCtx);
	if(!exportCtx)
	{
		exportJobActive = false;
		ShowExportResultMessage(-3, 0, 0);
		return;
	}

	IdInstalled_Export_Begin(exportCtx);

	MessageBox *waitBox = new MessageBox(MessageBox::BT_NOBUTTON, MessageBox::IT_ICONINFORMATION, true);
	waitBox->setTitle("id_installed.txt");
	waitBox->setMessage1("正在查询已安装游戏");
	waitBox->setMessage2("请稍候，期间可按 HOME");
	AppendModalMessageBox(waitBox);
}

void BrowserWindow::CloseIdModalImmediate(void)
{
	if(!idMessageOverlay)
		return;

	MainWindow *mainWindow = Application::instance()->getMainWindow();
	if(mainWindow)
		mainWindow->remove(idMessageOverlay);

	if(idMessageBox)
	{
		idMessageBox->messageOkClicked.disconnect(this);
		idMessageOverlay->remove(idMessageBox);
		delete idMessageBox;
		idMessageBox = nullptr;
	}

	delete idMessageOverlay;
	idMessageOverlay = nullptr;

	clearState(GuiElement::STATE_DISABLED);
}

void BrowserWindow::AppendModalMessageBox(MessageBox *box)
{
	if(!box)
		return;

	CloseIdModalImmediate();

	MainWindow *mainWindow = Application::instance()->getMainWindow();
	if(!mainWindow)
	{
		delete box;
		return;
	}

	idMessageBox = box;
	idMessageOverlay = new GuiFrame(0, 0);
	idMessageOverlay->setEffect(EFFECT_FADE, 10, 255);
	idMessageOverlay->setState(GuiElement::STATE_DISABLED);
	idMessageOverlay->effectFinished.connect(this, &BrowserWindow::OnModalMessageOverlayOpened);
	idMessageOverlay->append(box);

	mainWindow->append(idMessageOverlay);
}

void BrowserWindow::OnModalMessageOverlayOpened(GuiElement *element)
{
	element->effectFinished.disconnect(this);
	element->clearState(GuiElement::STATE_DISABLED);
}

void BrowserWindow::OnIdTxtMessageBoxClick(GuiElement *element, int val)
{
	(void)element;
	(void)val;

	if(!idMessageOverlay)
		return;

	idMessageOverlay->setEffect(EFFECT_FADE, -10, 255);
	idMessageOverlay->setState(GuiElement::STATE_DISABLED);
	idMessageOverlay->effectFinished.connect(this, &BrowserWindow::OnIdTxtMessageBoxClosed);
}

void BrowserWindow::OnIdTxtMessageBoxClosed(GuiElement *element)
{
	(void)element;
	CloseIdModalImmediate();
}

void BrowserWindow::OnScrollbarListChange(int selItem, int selIndex)
{
    selectedItem = selItem;
	pageIndex = selIndex;
	
	for(int i = 0; i < buttonCount; i++)
		folderButtons[i].folderButton->setPosition(0, 150 - (folderButtons[i].folderButtonImg->getHeight() + 30) * (i - pageIndex));
}
