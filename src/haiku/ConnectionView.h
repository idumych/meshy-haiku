// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_CONNECTIONVIEW_H
#define MESHY_HAIKU_CONNECTIONVIEW_H

#include <View.h>

#include "Engine.h"

class BButton;
class BListView;
class BMenuField;
class BPopUpMenu;
class BStringView;
class BTextControl;

// Picks a transport and opens the link.  Haiku has no Bluetooth LE stack, so
// only USB/UART serial and TCP are offered.
class ConnectionView : public BView {
public:
    explicit ConnectionView(meshy::Engine* engine);
    virtual ~ConnectionView();

    virtual void AttachedToWindow();
    virtual void MessageReceived(BMessage* message);

    void RefreshPorts();
    void SetConnectionState(int state);

private:
    void _UpdateEnabled();
    void _Connect();

    meshy::Engine* fEngine;
    BMenuField* fTransportField;
    BPopUpMenu* fTransportMenu;
    BListView* fPortList;
    BTextControl* fHostControl;
    BButton* fRefreshButton;
    BButton* fConnectButton;
    BButton* fDisconnectButton;
    BStringView* fHintView;
    int fState;
    std::vector<meshy::TransportDevice> fPorts;
};

#endif  // MESHY_HAIKU_CONNECTIONVIEW_H
