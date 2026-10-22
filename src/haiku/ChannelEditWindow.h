// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_CHANNELEDITWINDOW_H
#define MESHY_HAIKU_CHANNELEDITWINDOW_H

#include <Messenger.h>
#include <Window.h>

#include "Models.h"

class BButton;
class BStringView;
class BTextControl;

// Edits one channel slot: its name and its 16-byte pre-shared key.  Sends
// kMsgChannelSave back to the channels view on OK.
class ChannelEditWindow : public BWindow {
public:
    ChannelEditWindow(int index, const meshy::Channel& channel, const BMessenger& target);
    virtual ~ChannelEditWindow();

    virtual void MessageReceived(BMessage* message);

private:
    void _Save();
    void _UseHashtagKey();
    void _UsePublicKey();
    void _UpdateHint();

    int fIndex;
    BMessenger fTarget;
    BTextControl* fNameControl;
    BTextControl* fPskControl;
    BStringView* fHintView;
    BButton* fOkButton;
};

#endif  // MESHY_HAIKU_CHANNELEDITWINDOW_H
