// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_PACKETLOGVIEW_H
#define MESHY_HAIKU_PACKETLOGVIEW_H

#include <View.h>

#include "Engine.h"

class BButton;
class BCheckBox;
class BListView;
class BStringView;

// A rolling log of the raw radio packets the companion reports (0x88).
class PacketLogView : public BView {
public:
    explicit PacketLogView(meshy::Engine* engine);
    virtual ~PacketLogView();

    virtual void AttachedToWindow();
    virtual void MessageReceived(BMessage* message);

    void HandlePacket(BMessage* message);

private:
    meshy::Engine* fEngine;
    BListView* fList;
    BButton* fClearButton;
    BCheckBox* fPauseBox;
    BStringView* fCountView;
    int fTotal;
};

#endif  // MESHY_HAIKU_PACKETLOGVIEW_H
