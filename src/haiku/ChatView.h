// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_CHATVIEW_H
#define MESHY_HAIKU_CHATVIEW_H

#include <View.h>

#include <string>
#include <vector>

#include "Engine.h"

class BButton;
class BScrollView;
class BStringView;
class BTextView;

// A conversation with one contact.  The transcript is a read-only BTextView
// rather than a list of bubble items: it wraps and scrolls for free, and the
// text stays selectable and copyable, which a custom-drawn list would lose.
class ChatView : public BView {
public:
    explicit ChatView(meshy::Engine* engine);
    virtual ~ChatView();

    virtual void AttachedToWindow();
    virtual void MessageReceived(BMessage* message);

    void ShowContact(const std::string& contact_key_hex);
    void Reload();
    void OnMessageAdded(const std::string& contact_key_hex);
    void OnMessageStatus(int64_t message_id, int status);

private:
    void _AppendMessage(const meshy::Message& m, const meshy::Contact& contact);
    void _Send();
    void _UpdateHeader();
    void _UpdateCounter();

    meshy::Engine* fEngine;
    std::string fContactKey;
    BStringView* fHeaderView;
    BStringView* fRouteView;
    BTextView* fTranscript;
    BScrollView* fTranscriptScroll;
    BTextView* fInput;
    BButton* fSendButton;
    BStringView* fCounterView;
};

#endif  // MESHY_HAIKU_CHATVIEW_H
