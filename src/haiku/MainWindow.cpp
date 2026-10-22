// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MainWindow.h"

#include <Alert.h>
#include <Application.h>
#include <CardLayout.h>
#include <ControlLook.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>

#include "ChannelsView.h"
#include "ChatView.h"
#include "ConnectionView.h"
#include "ContactsView.h"
#include "DeviceView.h"
#include "MapView.h"
#include "PacketLogView.h"
#include "RepeaterView.h"
#include "SensorView.h"
#include "SettingsView.h"
#include "UiUtils.h"

using namespace meshy;

static const char* kNavLabels[kPageCount] = {
    "Connection", "Contacts", "Chat", "Channels", "Map", "Device", "Repeater", "Sensor",
    "Packets", "Settings",
};

MainWindow::MainWindow(Engine* engine)
    : BWindow(BRect(100, 100, 1060, 740), "Meshy", B_TITLED_WINDOW,
        B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
      fEngine(engine),
      fMenuBar(NULL),
      fNavList(NULL),
      fCardLayout(NULL),
      fStatusView(NULL),
      fBatteryView(NULL),
      fConnectionState(CONN_DISCONNECTED),
      fBatteryMv(0)
{
    _BuildMenu();
    _BuildLayout();
    _UpdateStatus();
    ShowPage(kPageConnection);
}

MainWindow::~MainWindow() {}

void MainWindow::_BuildMenu()
{
    fMenuBar = new BMenuBar("menubar");

    BMenu* appMenu = new BMenu("App");
    appMenu->AddItem(new BMenuItem("About Meshy…", new BMessage(B_ABOUT_REQUESTED)));
    appMenu->AddSeparatorItem();
    appMenu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
    fMenuBar->AddItem(appMenu);

    BMenu* deviceMenu = new BMenu("Device");
    deviceMenu->AddItem(new BMenuItem("Connect…", new BMessage(kMsgRefreshPorts)));
    deviceMenu->AddItem(new BMenuItem("Disconnect", new BMessage(kMsgDisconnect)));
    deviceMenu->AddSeparatorItem();
    deviceMenu->AddItem(new BMenuItem("Refresh contacts", new BMessage(kMsgRefreshContacts), 'R'));
    deviceMenu->AddItem(new BMenuItem("Send advert", new BMessage(kMsgSendAdvert)));
    deviceMenu->AddItem(new BMenuItem("Send flood advert", new BMessage(kMsgSendAdvertFlood)));
    deviceMenu->AddItem(new BMenuItem("Discover nodes", new BMessage(kMsgDiscoverNodes)));
    deviceMenu->AddSeparatorItem();
    deviceMenu->AddItem(new BMenuItem("Sync clock", new BMessage(kMsgSyncTime)));
    deviceMenu->AddItem(new BMenuItem("Reboot radio", new BMessage(kMsgDeviceReboot)));
    fMenuBar->AddItem(deviceMenu);
}

void MainWindow::_BuildLayout()
{
    // ── Navigation list ──
    fNavList = new BListView("nav", B_SINGLE_SELECTION_LIST);
    for (int i = 0; i < kPageCount; i++)
        fNavList->AddItem(new BStringItem(kNavLabels[i]));
    fNavList->SetSelectionMessage(new BMessage(kNavSelected));
    fNavList->SetExplicitMinSize(BSize(be_control_look->DefaultItemSpacing() * 9, B_SIZE_UNSET));

    BScrollView* navScroll = new BScrollView("navScroll", fNavList, 0, false, true,
        B_FANCY_BORDER);

    // ── Cards ──
    fConnectionView = new ConnectionView(fEngine);
    fContactsView = new ContactsView(fEngine);
    fChatView = new ChatView(fEngine);
    fChannelsView = new ChannelsView(fEngine);
    fMapView = new MapView(fEngine);
    fDeviceView = new DeviceView(fEngine);
    fRepeaterView = new RepeaterView(fEngine);
    fSensorView = new SensorView(fEngine);
    fPacketLogView = new PacketLogView(fEngine);
    fSettingsView = new SettingsView(fEngine);

    BView* cards = new BView("cards", 0);
    fCardLayout = new BCardLayout();
    cards->SetLayout(fCardLayout);
    // Order must match the NavPage enum.
    fCardLayout->AddView(fConnectionView);
    fCardLayout->AddView(fContactsView);
    fCardLayout->AddView(fChatView);
    fCardLayout->AddView(fChannelsView);
    fCardLayout->AddView(fMapView);
    fCardLayout->AddView(fDeviceView);
    fCardLayout->AddView(fRepeaterView);
    fCardLayout->AddView(fSensorView);
    fCardLayout->AddView(fPacketLogView);
    fCardLayout->AddView(fSettingsView);

    // ── Status strip ──
    fStatusView = new BStringView("status", "Disconnected");
    fBatteryView = new BStringView("battery", "");
    fBatteryView->SetAlignment(B_ALIGN_RIGHT);

    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .Add(fMenuBar)
        .AddGroup(B_HORIZONTAL, 0)
            .Add(navScroll, 0.0f)
            .Add(cards, 1.0f)
        .End()
        .Add(new BSeparatorView(B_HORIZONTAL))
        .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
            .SetInsets(B_USE_SMALL_INSETS, B_USE_SMALL_INSETS, B_USE_SMALL_INSETS,
                B_USE_SMALL_INSETS)
            .Add(fStatusView)
            .AddGlue()
            .Add(fBatteryView)
        .End()
    .End();
}

void MainWindow::ShowPage(NavPage page)
{
    if (page < 0 || page >= kPageCount)
        return;
    fCardLayout->SetVisibleItem(static_cast<int32>(page));
    if (fNavList->CurrentSelection(0) != static_cast<int32>(page))
        fNavList->Select(static_cast<int32>(page));
}

void MainWindow::ShowStatus(const BString& text)
{
    fSyncText = text;
    _UpdateStatus();
}

void MainWindow::_UpdateStatus()
{
    BString s = ui::ConnectionStateName(fConnectionState);
    if (fConnectionState == CONN_CONNECTED) {
        DeviceInfo info = fEngine->GetDeviceInfo();
        if (!info.name.empty())
            s << " — " << info.name.c_str();
        if (!info.model.empty())
            s << " (" << info.model.c_str() << ")";
    }
    if (fSyncText.Length() > 0)
        s << " — " << fSyncText;
    fStatusView->SetText(s.String());

    BString b;
    if (fBatteryMv > 0)
        b << ui::FormatVoltage(fBatteryMv);
    fBatteryView->SetText(b.String());
}

void MainWindow::_UpdateNavBadges()
{
    // Unread counts are shown next to the Contacts and Channels entries.
    int dm_unread = 0;
    std::vector<std::pair<std::string, int> > counts = fEngine->Store().AllUnreadCounts();
    for (size_t i = 0; i < counts.size(); i++)
        dm_unread += counts[i].second;

    int chan_unread = 0;
    std::vector<std::pair<int, int> > ccounts = fEngine->Store().AllChannelUnreadCounts();
    for (size_t i = 0; i < ccounts.size(); i++)
        chan_unread += ccounts[i].second;

    BStringItem* contacts = dynamic_cast<BStringItem*>(fNavList->ItemAt(kPageContacts));
    if (contacts != NULL) {
        BString label("Contacts");
        if (dm_unread > 0)
            label << "  (" << dm_unread << ")";
        contacts->SetText(label.String());
    }
    BStringItem* channels = dynamic_cast<BStringItem*>(fNavList->ItemAt(kPageChannels));
    if (channels != NULL) {
        BString label("Channels");
        if (chan_unread > 0)
            label << "  (" << chan_unread << ")";
        channels->SetText(label.String());
    }
    fNavList->Invalidate();
}

void MainWindow::_HandleConnectionState(BMessage* message)
{
    int32 state = CONN_DISCONNECTED;
    message->FindInt32("state", &state);
    fConnectionState = state;
    if (state != CONN_CONNECTED) {
        fSyncText = "";
        fBatteryMv = 0;
    }
    _UpdateStatus();
    fConnectionView->SetConnectionState(state);
    if (state == CONN_CONNECTED) {
        // The connection screen has done its job; move to the contact list.
        if (fCardLayout->VisibleIndex() == kPageConnection)
            ShowPage(kPageContacts);
    }
}

void MainWindow::_HandleSyncProgress(BMessage* message)
{
    bool active = false;
    message->FindBool("active", &active);
    if (!active) {
        fSyncText = "";
        _UpdateStatus();
        return;
    }
    int32 done = 0, total = 0;
    bool channels = false, messages = false;
    message->FindInt32("done", &done);
    message->FindInt32("total", &total);
    message->FindBool("channels", &channels);
    message->FindBool("messages", &messages);

    BString s;
    if (messages)
        s = "Syncing messages…";
    else if (channels)
        s = "Syncing channels…";
    else if (total > 0)
        s.SetToFormat("Syncing contacts %d/%d", static_cast<int>(done), static_cast<int>(total));
    else
        s = "Syncing…";
    fSyncText = s;
    _UpdateStatus();
}

void MainWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kNavSelected: {
            int32 index = fNavList->CurrentSelection(0);
            if (index >= 0)
                fCardLayout->SetVisibleItem(index);
            break;
        }

        // ── Engine events ──
        case kEngineConnectionState:
            _HandleConnectionState(message);
            break;

        case kEngineSyncProgress:
            _HandleSyncProgress(message);
            break;

        case kEngineToast:
        case kEngineError: {
            const char* text = NULL;
            if (message->FindString("text", &text) == B_OK && text != NULL) {
                // Deliberately not a modal alert: BAlert::Go() blocks this
                // looper, which would stall every further engine event while
                // it is open.
                BString s;
                if (message->what == kEngineError)
                    s << "Error: ";
                s << text;
                ShowStatus(s);
            }
            break;
        }

        case kEngineDeviceInfo:
            _UpdateStatus();
            fDeviceView->Reload();
            fSettingsView->Reload();
            break;

        case kEngineContactsChanged:
            fContactsView->Reload();
            fChatView->Reload();
            fMapView->Reload();
            _UpdateNavBadges();
            break;

        case kEngineChannelsChanged:
            fChannelsView->Reload();
            _UpdateNavBadges();
            break;

        case kEngineDiscoveredChanged:
            fContactsView->ReloadDiscovered();
            fMapView->Reload();
            break;

        case kEngineMessage: {
            const char* contact = NULL;
            message->FindString("contact", &contact);
            fChatView->OnMessageAdded(contact != NULL ? contact : "");
            fContactsView->Reload();
            _UpdateNavBadges();
            break;
        }

        case kEngineMessageStatus: {
            int64 id = 0;
            int32 status = 0;
            message->FindInt64("id", &id);
            message->FindInt32("status", &status);
            fChatView->OnMessageStatus(id, status);
            break;
        }

        case kEngineChannelMessage: {
            int32 channel = 0;
            message->FindInt32("channel", &channel);
            fChannelsView->OnMessageAdded(channel);
            _UpdateNavBadges();
            break;
        }

        case kEngineBattery: {
            int32 mv = 0, used = 0, total = 0;
            message->FindInt32("mv", &mv);
            message->FindInt32("used_kb", &used);
            message->FindInt32("total_kb", &total);
            fBatteryMv = mv;
            _UpdateStatus();
            fDeviceView->SetBattery(mv, used, total);
            break;
        }

        case kEngineStats:
            fDeviceView->HandleStats(message);
            break;

        case kEngineCliReply:
            fRepeaterView->AppendLocalCliReply(message);
            break;

        case kEngineRemoteCliReply:
            fRepeaterView->AppendRemoteCliReply(message);
            break;

        case kEngineLoginResult:
            fRepeaterView->HandleLoginResult(message);
            break;

        case kEngineStatusResponse:
            fRepeaterView->HandleStatusResponse(message);
            break;

        case kEngineTelemetry:
            fSensorView->HandleTelemetry(message);
            break;

        case kEngineRawPacket:
            fPacketLogView->HandlePacket(message);
            break;

        case kEngineTraceResult:
            fDeviceView->HandleTrace(message);
            break;

        // ── Navigation requests from views ──
        case kShowChat: {
            const char* contact = NULL;
            if (message->FindString("contact", &contact) == B_OK && contact != NULL) {
                fChatView->ShowContact(contact);
                ShowPage(kPageChat);
                _UpdateNavBadges();
            }
            break;
        }

        case kShowRepeater: {
            const char* contact = NULL;
            if (message->FindString("contact", &contact) == B_OK && contact != NULL) {
                fRepeaterView->ShowContact(contact);
                ShowPage(kPageRepeater);
            }
            break;
        }

        case kShowSensor: {
            const char* contact = NULL;
            if (message->FindString("contact", &contact) == B_OK && contact != NULL) {
                fSensorView->ShowContact(contact);
                ShowPage(kPageSensor);
            }
            break;
        }

        case kShowChannel: {
            int32 channel = 0;
            if (message->FindInt32("channel", &channel) == B_OK) {
                fChannelsView->SelectChannel(channel);
                ShowPage(kPageChannels);
            }
            break;
        }

        // ── Menu shortcuts ──
        case kMsgRefreshPorts:
            ShowPage(kPageConnection);
            fConnectionView->RefreshPorts();
            break;

        case kMsgDisconnect:
            fEngine->Disconnect();
            break;

        case kMsgRefreshContacts:
            fEngine->RefreshContacts();
            break;

        case kMsgSendAdvert:
            fEngine->SendAdvert(false);
            ShowStatus(BString("Advert sent"));
            break;

        case kMsgSendAdvertFlood:
            fEngine->SendAdvert(true);
            ShowStatus(BString("Flood advert sent"));
            break;

        case kMsgDiscoverNodes:
            fEngine->SendDiscoverRequest();
            ShowStatus(BString("Discovery request sent"));
            break;

        case kMsgSyncTime:
            fEngine->SetDeviceTime(0);  // 0 means "use the host clock"
            ShowStatus(BString("Clock synced"));
            break;

        case kMsgDeviceReboot: {
            BAlert* alert = new BAlert("Meshy", "Reboot the connected radio?", "Cancel", "Reboot",
                NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
            alert->SetShortcut(0, B_ESCAPE);
            if (alert->Go() == 1)
                fEngine->Reboot();
            break;
        }

        case B_ABOUT_REQUESTED:
            be_app->PostMessage(B_ABOUT_REQUESTED);
            break;

        default:
            BWindow::MessageReceived(message);
            break;
    }
}

bool MainWindow::QuitRequested()
{
    be_app->PostMessage(B_QUIT_REQUESTED);
    return true;
}
