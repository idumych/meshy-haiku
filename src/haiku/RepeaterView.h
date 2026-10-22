// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_REPEATERVIEW_H
#define MESHY_HAIKU_REPEATERVIEW_H

#include <View.h>

#include <string>

#include "Engine.h"

class BButton;
class BStringView;
class BTextControl;
class BTextView;

// Administration console for a repeater or room server: log in, run CLI
// commands and read the node's status.
class RepeaterView : public BView {
public:
    explicit RepeaterView(meshy::Engine* engine);
    virtual ~RepeaterView();

    virtual void AttachedToWindow();
    virtual void MessageReceived(BMessage* message);

    void ShowContact(const std::string& contact_key_hex);
    void AppendLocalCliReply(BMessage* message);
    void AppendRemoteCliReply(BMessage* message);
    void HandleLoginResult(BMessage* message);
    void HandleStatusResponse(BMessage* message);

private:
    void _Append(const BString& text, bool is_input);
    void _SendCommand();
    void _UpdateHeader();

    meshy::Engine* fEngine;
    std::string fContactKey;
    bool fLoggedIn;
    BStringView* fHeaderView;
    BStringView* fStateView;
    BTextControl* fPasswordControl;
    BButton* fLoginButton;
    BButton* fLogoutButton;
    BButton* fStatusButton;
    BButton* fNeighborsButton;
    BTextView* fConsole;
    BTextControl* fCommandControl;
    BButton* fSendButton;
};

#endif  // MESHY_HAIKU_REPEATERVIEW_H
