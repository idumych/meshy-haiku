// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChannelsView.h"

#include <Alert.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>

#include <math.h>

#include <map>

#include "ChannelEditWindow.h"
#include "UiMessages.h"
#include "UiUtils.h"

using namespace meshy;

// ─── ChannelItem ─────────────────────────────────────────────────

ChannelItem::ChannelItem(const Channel& channel, int unread)
    : BListItem(), fChannel(channel), fUnread(unread), fBaselineOffset(0)
{
}

void ChannelItem::Update(BView* owner, const BFont* font)
{
    BListItem::Update(owner, font);
    font_height fh;
    font->GetHeight(&fh);
    fBaselineOffset = ceilf(fh.ascent) + 4.0f;
    SetHeight(ceilf(fh.ascent + fh.descent + fh.leading) + 10.0f);
}

void ChannelItem::DrawItem(BView* owner, BRect frame, bool complete)
{
    if (IsSelected() || complete) {
        rgb_color color = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
                                       : owner->ViewColor();
        owner->SetLowColor(color);
        owner->FillRect(frame, B_SOLID_LOW);
    }
    rgb_color textColor = IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
                                       : ui_color(B_LIST_ITEM_TEXT_COLOR);
    owner->SetHighColor(textColor);
    owner->SetFont(fUnread > 0 ? be_bold_font : be_plain_font);

    BString label;
    label << "#" << fChannel.DisplayName().c_str();
    if (fUnread > 0)
        label << "  (" << fUnread << ")";
    BString truncated(label);
    owner->TruncateString(&truncated, B_TRUNCATE_END, frame.Width() - 16.0f);
    owner->DrawString(truncated.String(), BPoint(frame.left + 8.0f, frame.top + fBaselineOffset));
    owner->SetFont(be_plain_font);
}

// ─── ChannelsView ────────────────────────────────────────────────

ChannelsView::ChannelsView(Engine* engine)
    : BView("channels", B_WILL_DRAW), fEngine(engine), fSelected(-1)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    fList = new BListView("channelList", B_SINGLE_SELECTION_LIST);
    fList->SetSelectionMessage(new BMessage(kMsgChannelSelected));
    BScrollView* listScroll = new BScrollView("channelScroll", fList, 0, false, true,
        B_FANCY_BORDER);
    listScroll->SetExplicitMinSize(BSize(160, B_SIZE_UNSET));
    listScroll->SetExplicitMaxSize(BSize(220, B_SIZE_UNLIMITED));

    fAddButton = new BButton("add", "Add…", new BMessage(kMsgChannelAdd));
    fEditButton = new BButton("edit", "Edit…", new BMessage(kMsgChannelEdit));
    fRemoveButton = new BButton("remove", "Remove", new BMessage(kMsgChannelRemove));

    fHeaderView = new BStringView("channelHeader", "No channel selected");
    BFont bold(be_bold_font);
    bold.SetSize(bold.Size() * 1.2f);
    fHeaderView->SetFont(&bold);

    fTranscript = new BTextView("channelTranscript");
    fTranscript->MakeEditable(false);
    fTranscript->SetStylable(true);
    fTranscript->SetWordWrap(true);
    fTranscript->SetViewUIColor(B_LIST_BACKGROUND_COLOR);
    fTranscript->SetInsets(8, 8, 8, 8);
    BScrollView* transcriptScroll = new BScrollView("chTranscriptScroll", fTranscript, 0, false,
        true, B_FANCY_BORDER);

    fInput = new BTextView("channelInput");
    fInput->SetWordWrap(true);
    fInput->SetInsets(4, 4, 4, 4);
    float lineHeight = ceilf(be_plain_font->Size() * 1.4f);
    BScrollView* inputScroll = new BScrollView("chInputScroll", fInput, 0, false, true,
        B_FANCY_BORDER);
    inputScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, lineHeight * 2.5f));
    inputScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, lineHeight * 2.5f));

    fSendButton = new BButton("chSend", "Send", new BMessage(kMsgChannelSend));

    BLayoutBuilder::Group<>(this, B_HORIZONTAL, B_USE_DEFAULT_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 0.0f)
            .Add(listScroll, 1.0f)
            .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
                .Add(fAddButton)
                .Add(fEditButton)
            .End()
            .Add(fRemoveButton)
        .End()
        .AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 1.0f)
            .Add(fHeaderView)
            .Add(transcriptScroll, 1.0f)
            .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
                .Add(inputScroll, 1.0f)
                .Add(fSendButton)
            .End()
        .End()
    .End();
}

ChannelsView::~ChannelsView() {}

void ChannelsView::AttachedToWindow()
{
    BView::AttachedToWindow();
    fList->SetTarget(this);
    fSendButton->SetTarget(this);
    fAddButton->SetTarget(this);
    fEditButton->SetTarget(this);
    fRemoveButton->SetTarget(this);
    Reload();
}

void ChannelsView::Reload()
{
    for (int32 i = fList->CountItems() - 1; i >= 0; i--)
        delete fList->RemoveItem(i);

    std::map<int, int> unread;
    std::vector<std::pair<int, int> > counts = fEngine->Store().AllChannelUnreadCounts();
    for (size_t i = 0; i < counts.size(); i++)
        unread[counts[i].first] = counts[i].second;

    std::vector<Channel> channels = fEngine->Store().GetChannels();
    int32 restore = -1;
    for (size_t i = 0; i < channels.size(); i++) {
        std::map<int, int>::iterator it = unread.find(channels[i].index);
        fList->AddItem(new ChannelItem(channels[i], it != unread.end() ? it->second : 0));
        if (channels[i].index == fSelected)
            restore = static_cast<int32>(i);
    }
    if (restore >= 0)
        fList->Select(restore);
    else if (!channels.empty() && fSelected < 0)
        fList->Select(0);

    _ReloadTranscript();
}

void ChannelsView::SelectChannel(int index)
{
    fSelected = index;
    for (int32 i = 0; i < fList->CountItems(); i++) {
        ChannelItem* item = dynamic_cast<ChannelItem*>(fList->ItemAt(i));
        if (item != NULL && item->Get().index == index) {
            fList->Select(i);
            break;
        }
    }
    _ReloadTranscript();
}

void ChannelsView::_ReloadTranscript()
{
    int32 sel = fList->CurrentSelection(0);
    ChannelItem* item = sel >= 0 ? dynamic_cast<ChannelItem*>(fList->ItemAt(sel)) : NULL;
    if (item == NULL) {
        fSelected = -1;
        fHeaderView->SetText("No channel selected");
        fTranscript->SetText("");
        fSendButton->SetEnabled(false);
        fEditButton->SetEnabled(false);
        fRemoveButton->SetEnabled(false);
        return;
    }

    fSelected = item->Get().index;
    fEngine->Store().MarkChannelRead(fSelected);

    BString header;
    header << "#" << item->Get().DisplayName().c_str();
    if (!item->Get().region.empty())
        header << "  ·  " << item->Get().region.c_str();
    fHeaderView->SetText(header.String());
    fSendButton->SetEnabled(true);
    fEditButton->SetEnabled(true);
    fRemoveButton->SetEnabled(true);

    fTranscript->SetText("");
    std::vector<ChannelMessage> msgs = fEngine->Store().GetChannelMessages(fSelected, 500);
    for (size_t i = 0; i < msgs.size(); i++) {
        const ChannelMessage& m = msgs[i];
        int32 start = fTranscript->TextLength();

        BString head;
        head << ui::FormatClock(m.timestamp) << "  ";
        head << (m.is_outgoing ? "You" : (m.sender_name.empty() ? "(unknown)"
                                                                : m.sender_name.c_str()));
        if (m.has_snr)
            head << "  ·  " << ui::FormatSnr(m.snr);
        if (m.path_len >= 0)
            head << "  ·  " << ui::FormatPath(m.path_len);
        head << "\n";
        fTranscript->Insert(head.String());

        BFont small(be_bold_font);
        small.SetSize(ceilf(be_plain_font->Size() * 0.9f));
        rgb_color headColor = m.is_outgoing
            ? ui_color(B_CONTROL_HIGHLIGHT_COLOR)
            : tint_color(ui_color(B_LIST_ITEM_TEXT_COLOR), B_DARKEN_1_TINT);
        fTranscript->SetFontAndColor(start, fTranscript->TextLength(), &small, B_FONT_ALL,
            &headColor);

        int32 bodyStart = fTranscript->TextLength();
        BString body;
        body << m.text.c_str() << "\n\n";
        fTranscript->Insert(body.String());
        rgb_color bodyColor = ui_color(B_LIST_ITEM_TEXT_COLOR);
        fTranscript->SetFontAndColor(bodyStart, fTranscript->TextLength(), be_plain_font,
            B_FONT_ALL, &bodyColor);
    }
    fTranscript->ScrollToOffset(fTranscript->TextLength());
}

void ChannelsView::OnMessageAdded(int channel_index)
{
    if (channel_index == fSelected)
        _ReloadTranscript();
    else
        Reload();  // refresh the unread badge in the list
}

void ChannelsView::_Send()
{
    if (fSelected < 0)
        return;
    BString text(fInput->Text());
    text.Trim();
    if (text.Length() == 0)
        return;
    fEngine->SendChannelMessage(fSelected, ui::Std(text));
    fInput->SetText("");
}

void ChannelsView::_EditChannel(bool create)
{
    int index = fSelected;
    Channel existing;
    bool have = false;
    if (!create && fSelected >= 0)
        have = fEngine->Store().GetChannel(fSelected, &existing);

    if (create) {
        // Pick the first free slot the radio will accept.
        std::vector<Channel> channels = fEngine->Store().GetChannels();
        index = 0;
        for (int i = 0; i < 32; i++) {
            bool used = false;
            for (size_t j = 0; j < channels.size(); j++) {
                if (channels[j].index == i) {
                    used = true;
                    break;
                }
            }
            if (!used) {
                index = i;
                break;
            }
        }
        existing = Channel();
        existing.index = index;
    }

    ChannelEditWindow* win = new ChannelEditWindow(index, have || !create ? existing : Channel(),
        BMessenger(this));
    win->Show();
}

void ChannelsView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgChannelSelected:
            _ReloadTranscript();
            break;

        case kMsgChannelSend:
            _Send();
            break;

        case kMsgChannelAdd:
            _EditChannel(true);
            break;

        case kMsgChannelEdit:
            if (fSelected >= 0)
                _EditChannel(false);
            break;

        case kMsgChannelSave: {
            int32 index = 0;
            const char* name = NULL;
            const char* psk_hex = NULL;
            if (message->FindInt32("index", &index) != B_OK)
                break;
            message->FindString("name", &name);
            message->FindString("psk", &psk_hex);
            ByteBuffer psk = FromHexOrEmpty(psk_hex != NULL ? psk_hex : "");
            psk.resize(16, 0);
            fEngine->SetChannel(index, name != NULL ? name : "", psk);
            fSelected = index;
            break;
        }

        case kMsgChannelRemove: {
            if (fSelected < 0)
                break;
            BAlert* alert = new BAlert("Meshy", "Remove this channel and its messages?", "Cancel",
                "Remove", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
            alert->SetShortcut(0, B_ESCAPE);
            if (alert->Go() == 1) {
                fEngine->ClearChannel(fSelected);
                fSelected = -1;
            }
            break;
        }

        default:
            BView::MessageReceived(message);
            break;
    }
}
