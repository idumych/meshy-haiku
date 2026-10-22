// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChatView.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>

#include <math.h>

#include "UiMessages.h"
#include "UiUtils.h"

using namespace meshy;

ChatView::ChatView(Engine* engine) : BView("chat", B_WILL_DRAW), fEngine(engine)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    fHeaderView = new BStringView("chatHeader", "No conversation selected");
    BFont bold(be_bold_font);
    bold.SetSize(bold.Size() * 1.2f);
    fHeaderView->SetFont(&bold);

    fRouteView = new BStringView("chatRoute", "");
    fRouteView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

    fTranscript = new BTextView("transcript");
    fTranscript->MakeEditable(false);
    fTranscript->SetStylable(true);
    fTranscript->SetWordWrap(true);
    fTranscript->SetViewUIColor(B_LIST_BACKGROUND_COLOR);
    fTranscript->SetInsets(8, 8, 8, 8);
    fTranscriptScroll = new BScrollView("transcriptScroll", fTranscript, 0, false, true,
        B_FANCY_BORDER);

    fInput = new BTextView("input");
    fInput->SetWordWrap(true);
    fInput->SetInsets(4, 4, 4, 4);
    // Roughly three lines tall, so a long message is still comfortable.
    float lineHeight = ceilf(be_plain_font->Size() * 1.4f);
    BScrollView* inputScroll = new BScrollView("inputScroll", fInput, 0, false, true,
        B_FANCY_BORDER);
    inputScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, lineHeight * 3));
    inputScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, lineHeight * 3));

    fSendButton = new BButton("send", "Send", new BMessage(kMsgSendMessage));
    fCounterView = new BStringView("counter", "");
    fCounterView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .Add(fHeaderView)
        .Add(fRouteView)
        .Add(fTranscriptScroll, 1.0f)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
            .Add(inputScroll, 1.0f)
            .AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
                .Add(fSendButton)
                .Add(fCounterView)
                .AddGlue()
            .End()
        .End()
    .End();
}

ChatView::~ChatView() {}

void ChatView::AttachedToWindow()
{
    BView::AttachedToWindow();
    ui::RetargetControls(this, this);
    _UpdateHeader();
}

void ChatView::ShowContact(const std::string& contact_key_hex)
{
    fContactKey = contact_key_hex;
    // Opening a conversation clears its unread state.
    fEngine->Store().MarkContactRead(fContactKey);
    Reload();
    fInput->MakeFocus(true);
}

void ChatView::_UpdateHeader()
{
    if (fContactKey.empty()) {
        fHeaderView->SetText("No conversation selected");
        fRouteView->SetText("Pick a contact to start chatting.");
        fSendButton->SetEnabled(false);
        return;
    }
    Contact c;
    if (!fEngine->Store().GetContact(fContactKey, &c)) {
        fHeaderView->SetText("Unknown contact");
        fRouteView->SetText("");
        fSendButton->SetEnabled(false);
        return;
    }

    fHeaderView->SetText(c.DisplayName().c_str());

    BString route;
    route << ui::ContactTypeName(c.type) << " · " << ui::FormatPath(c.path_length);
    if (HasTime(c.last_seen))
        route << " · heard " << ui::FormatRelative(c.last_seen);
    route << " · " << c.KeyPrefix(6).c_str();
    fRouteView->SetText(route.String());
    fSendButton->SetEnabled(true);
    _UpdateCounter();
}

void ChatView::_UpdateCounter()
{
    size_t limit = MaxDmTextBytes();
    size_t used = strlen(fInput->Text());
    BString s;
    s << static_cast<int>(limit > used ? limit - used : 0);
    fCounterView->SetText(s.String());
}

void ChatView::_AppendMessage(const Message& m, const Contact& contact)
{
    int32 start = fTranscript->TextLength();

    BString header;
    header << ui::FormatClock(m.timestamp) << "  ";
    header << (m.is_outgoing ? "You" : contact.DisplayName().c_str());
    if (m.is_outgoing) {
        // A short status marker keeps delivery state visible in the flow.
        switch (m.status) {
            case MSG_PENDING: header << "  ·  sending"; break;
            case MSG_SENT: header << "  ·  sent"; break;
            case MSG_DELIVERED: header << "  ·  delivered"; break;
            case MSG_FAILED: header << "  ·  FAILED"; break;
            default: break;
        }
    }
    header << "\n";
    fTranscript->Insert(header.String());

    BFont small(be_bold_font);
    small.SetSize(ceilf(be_plain_font->Size() * 0.9f));
    rgb_color headerColor = m.is_outgoing ? ui_color(B_CONTROL_HIGHLIGHT_COLOR)
                                          : tint_color(ui_color(B_LIST_ITEM_TEXT_COLOR),
                                                B_DARKEN_1_TINT);
    if (m.status == MSG_FAILED)
        headerColor = (rgb_color){ 200, 40, 40, 255 };
    fTranscript->SetFontAndColor(start, fTranscript->TextLength(), &small, B_FONT_ALL,
        &headerColor);

    int32 bodyStart = fTranscript->TextLength();
    BString body;
    body << m.text.c_str() << "\n\n";
    fTranscript->Insert(body.String());
    rgb_color bodyColor = ui_color(B_LIST_ITEM_TEXT_COLOR);
    fTranscript->SetFontAndColor(bodyStart, fTranscript->TextLength(), be_plain_font, B_FONT_ALL,
        &bodyColor);
}

void ChatView::Reload()
{
    _UpdateHeader();

    fTranscript->SetText("");
    if (fContactKey.empty())
        return;

    Contact c;
    if (!fEngine->Store().GetContact(fContactKey, &c))
        return;

    // The last few hundred messages are plenty for a transcript view.
    std::vector<Message> msgs = fEngine->Store().GetMessages(fContactKey, 500);
    for (size_t i = 0; i < msgs.size(); i++)
        _AppendMessage(msgs[i], c);

    // Scroll to the newest message.
    fTranscript->ScrollToOffset(fTranscript->TextLength());
}

void ChatView::OnMessageAdded(const std::string& contact_key_hex)
{
    if (fContactKey.empty() || contact_key_hex != fContactKey)
        return;
    // Keep the conversation marked read while it is on screen.
    if (!IsHidden())
        fEngine->Store().MarkContactRead(fContactKey);
    Reload();
}

void ChatView::OnMessageStatus(int64_t message_id, int status)
{
    (void)message_id;
    (void)status;
    // Status is rendered inline with each message, so redraw the transcript.
    if (!fContactKey.empty())
        Reload();
}

void ChatView::_Send()
{
    if (fContactKey.empty())
        return;
    BString text(fInput->Text());
    text.Trim();
    if (text.Length() == 0)
        return;
    fEngine->SendMessage(fContactKey, ui::Std(text));
    fInput->SetText("");
    _UpdateCounter();
}

void ChatView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgSendMessage:
            _Send();
            break;
        default:
            BView::MessageReceived(message);
            break;
    }
}
