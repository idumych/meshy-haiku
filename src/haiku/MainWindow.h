// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The application window: a navigation list on the left, a card layout of
// views on the right, and a status strip along the bottom.  Every engine
// event reaches this window as a BMessage (see EngineBridge), so all UI work
// happens on the window thread.

#ifndef MESHY_HAIKU_MAINWINDOW_H
#define MESHY_HAIKU_MAINWINDOW_H

#include <Window.h>

#include <string>

#include "Engine.h"
#include "UiMessages.h"

class BCardLayout;
class BListView;
class BStringView;
class BMenuBar;

class ConnectionView;
class ContactsView;
class ChatView;
class ChannelsView;
class MapView;
class DeviceView;
class RepeaterView;
class SensorView;
class PacketLogView;
class SettingsView;

class MainWindow : public BWindow {
public:
    explicit MainWindow(meshy::Engine* engine);
    virtual ~MainWindow();

    virtual void MessageReceived(BMessage* message);
    virtual bool QuitRequested();

    meshy::Engine* GetEngine() const { return fEngine; }
    // Switches the visible card and keeps the nav list in step.
    void ShowPage(NavPage page);
    void ShowStatus(const BString& text);

private:
    void _BuildMenu();
    void _BuildLayout();
    void _UpdateStatus();
    void _UpdateNavBadges();
    void _HandleConnectionState(BMessage* message);
    void _HandleSyncProgress(BMessage* message);

    meshy::Engine* fEngine;

    BMenuBar* fMenuBar;
    BListView* fNavList;
    BCardLayout* fCardLayout;
    BStringView* fStatusView;
    BStringView* fBatteryView;

    ConnectionView* fConnectionView;
    ContactsView* fContactsView;
    ChatView* fChatView;
    ChannelsView* fChannelsView;
    MapView* fMapView;
    DeviceView* fDeviceView;
    RepeaterView* fRepeaterView;
    SensorView* fSensorView;
    PacketLogView* fPacketLogView;
    SettingsView* fSettingsView;

    int fConnectionState;
    BString fSyncText;
    int fBatteryMv;
};

#endif  // MESHY_HAIKU_MAINWINDOW_H
