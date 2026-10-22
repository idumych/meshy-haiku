// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ConnectionView.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include "UiMessages.h"
#include "UiUtils.h"

using namespace meshy;

ConnectionView::ConnectionView(Engine* engine)
    : BView("connection", B_WILL_DRAW),
      fEngine(engine),
      fState(CONN_DISCONNECTED)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    fTransportMenu = new BPopUpMenu("transport");
    BMenuItem* serial = new BMenuItem("USB / serial", new BMessage(kMsgTransportChanged));
    BMenuItem* tcp = new BMenuItem("TCP (Wi-Fi)", new BMessage(kMsgTransportChanged));
    fTransportMenu->AddItem(serial);
    fTransportMenu->AddItem(tcp);
    serial->SetMarked(true);
    fTransportField = new BMenuField("transportField", "Connect over:", fTransportMenu);

    fPortList = new BListView("ports", B_SINGLE_SELECTION_LIST);
    fPortList->SetInvocationMessage(new BMessage(kMsgConnect));
    BScrollView* portScroll = new BScrollView("portScroll", fPortList, 0, false, true,
        B_FANCY_BORDER);
    portScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 140));

    fHostControl = new BTextControl("host", "Address:", "192.168.1.50:5000", NULL);

    fRefreshButton = new BButton("refresh", "Rescan ports", new BMessage(kMsgRefreshPorts));
    fConnectButton = new BButton("connect", "Connect", new BMessage(kMsgConnect));
    fDisconnectButton = new BButton("disconnect", "Disconnect", new BMessage(kMsgDisconnect));

    fHintView = new BStringView("hint",
        "Bluetooth LE is not available on Haiku; use a USB cable or a Wi-Fi bridge.");
    fHintView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

    BStringView* title = new BStringView("title", "Connect to a MeshCore radio");
    BFont bold(be_bold_font);
    bold.SetSize(bold.Size() * 1.3f);
    title->SetFont(&bold);

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .Add(title)
        .Add(fTransportField)
        .Add(portScroll)
        .Add(fHostControl)
        .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
            .Add(fRefreshButton)
            .AddGlue()
            .Add(fDisconnectButton)
            .Add(fConnectButton)
        .End()
        .Add(fHintView)
        .AddGlue()
    .End();
}

ConnectionView::~ConnectionView() {}

void ConnectionView::AttachedToWindow()
{
    BView::AttachedToWindow();
    fTransportField->Menu()->SetTargetForItems(this);
    fPortList->SetTarget(this);
    fRefreshButton->SetTarget(this);
    fConnectButton->SetTarget(this);
    fDisconnectButton->SetTarget(this);
    RefreshPorts();
    _UpdateEnabled();
}

void ConnectionView::RefreshPorts()
{
    fPortList->MakeEmpty();
    fPorts = fEngine->EnumerateSerial();
    for (size_t i = 0; i < fPorts.size(); i++) {
        BString label(fPorts[i].path.c_str());
        if (!fPorts[i].name.empty() && fPorts[i].name != fPorts[i].path)
            label << "  (" << fPorts[i].name.c_str() << ")";
        fPortList->AddItem(new BStringItem(label.String()));
    }
    if (fPorts.empty())
        fPortList->AddItem(new BStringItem("No serial ports found"));
    else
        fPortList->Select(0);
}

void ConnectionView::SetConnectionState(int state)
{
    fState = state;
    _UpdateEnabled();
}

void ConnectionView::_UpdateEnabled()
{
    bool connected = (fState == CONN_CONNECTED);
    bool busy = (fState == CONN_CONNECTING || fState == CONN_DISCONNECTING);
    bool serial = (fTransportMenu->IndexOf(fTransportMenu->FindMarked()) == 0);

    fConnectButton->SetEnabled(!connected && !busy);
    fDisconnectButton->SetEnabled(connected);
    fRefreshButton->SetEnabled(serial && !connected);
    fPortList->SetEnabled(serial && !connected);
    fHostControl->SetEnabled(!serial && !connected);
}

void ConnectionView::_Connect()
{
    bool serial = (fTransportMenu->IndexOf(fTransportMenu->FindMarked()) == 0);
    if (serial) {
        int32 index = fPortList->CurrentSelection(0);
        if (index < 0 || static_cast<size_t>(index) >= fPorts.size())
            return;
        fEngine->Connect(TRANSPORT_SERIAL, fPorts[index].path);
    } else {
        BString host(fHostControl->Text());
        host.Trim();
        if (host.Length() == 0)
            return;
        fEngine->Connect(TRANSPORT_TCP, ui::Std(host));
    }
}

void ConnectionView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgTransportChanged:
            _UpdateEnabled();
            break;
        case kMsgRefreshPorts:
            RefreshPorts();
            break;
        case kMsgConnect:
            _Connect();
            break;
        case kMsgDisconnect:
            fEngine->Disconnect();
            break;
        default:
            BView::MessageReceived(message);
            break;
    }
}
