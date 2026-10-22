// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PacketLogView.h"

#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringView.h>
#include <Window.h>

#include <stdio.h>
#include <time.h>

#include "MeshPacket.h"
#include "UiMessages.h"
#include "UiUtils.h"

using namespace meshy;

// Keep the log bounded; this view is for spotting patterns, not archiving.
static const int32 kMaxRows = 500;

static const char* _PayloadTypeName(int type)
{
    switch (type) {
        case 0x00: return "REQ";
        case 0x01: return "RESPONSE";
        case 0x02: return "TXT_MSG";
        case 0x03: return "ACK";
        case PAYLOAD_ADVERT: return "ADVERT";
        case PAYLOAD_GROUP_TEXT: return "GROUP_TEXT";
        case 0x06: return "GROUP_DATA";
        case 0x07: return "ANON_REQ";
        case 0x08: return "PATH";
        case 0x09: return "TRACE";
        case 0x0A: return "RAW";
        default: return "?";
    }
}

static const char* _RouteTypeName(int type)
{
    switch (type) {
        case 0: return "flood";
        case 1: return "direct";
        case 2: return "transport-flood";
        case 3: return "transport-direct";
        default: return "?";
    }
}

PacketLogView::PacketLogView(Engine* engine)
    : BView("packets", B_WILL_DRAW), fEngine(engine), fTotal(0)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    BStringView* title = new BStringView("pktTitle", "Packets heard");
    BFont bold(be_bold_font);
    bold.SetSize(bold.Size() * 1.2f);
    title->SetFont(&bold);

    fList = new BListView("packetList", B_SINGLE_SELECTION_LIST);
    fList->SetFont(be_fixed_font);
    BScrollView* scroll = new BScrollView("pktScroll", fList, 0, false, true, B_FANCY_BORDER);

    fClearButton = new BButton("clear", "Clear", new BMessage(kMsgPacketsClear));
    fPauseBox = new BCheckBox("pause", "Pause", new BMessage(kMsgPacketsPause));
    fCountView = new BStringView("pktCount", "0 packets");
    fCountView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .Add(title)
        .Add(scroll, 1.0f)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
            .Add(fClearButton)
            .Add(fPauseBox)
            .AddGlue()
            .Add(fCountView)
        .End()
    .End();
}

PacketLogView::~PacketLogView() {}

void PacketLogView::AttachedToWindow()
{
    BView::AttachedToWindow();
    ui::RetargetControls(this, this);
}

void PacketLogView::HandlePacket(BMessage* message)
{
    if (fPauseBox->Value() == B_CONTROL_ON)
        return;

    int32 payloadType = 0, routeType = 0, hops = 0;
    const char* hash = NULL;
    const char* payload = NULL;
    message->FindInt32("payload_type", &payloadType);
    message->FindInt32("route_type", &routeType);
    message->FindInt32("hops", &hops);
    message->FindString("hash", &hash);
    message->FindString("payload", &payload);

    time_t now = time(NULL);
    struct tm tm_val;
    localtime_r(&now, &tm_val);
    char clock[16];
    strftime(clock, sizeof(clock), "%H:%M:%S", &tm_val);

    BString line;
    line.SetToFormat("%s  %-11s %-17s %d hop%s", clock, _PayloadTypeName(payloadType),
        _RouteTypeName(routeType), static_cast<int>(hops), hops == 1 ? " " : "s");

    const char* advertName = NULL;
    if (message->FindString("advert_name", &advertName) == B_OK && advertName != NULL
        && advertName[0] != '\0') {
        line << "  " << advertName;
    } else if (payload != NULL) {
        BString p(payload);
        if (p.Length() > 24)
            p.Truncate(24);
        line << "  " << p << "…";
    }

    fList->AddItem(new BStringItem(line.String()));
    fTotal++;

    // Trim from the top so memory stays bounded during a long session.
    while (fList->CountItems() > kMaxRows)
        delete fList->RemoveItem(static_cast<int32>(0));

    // Follow the tail only when the user is already at the bottom.
    fList->ScrollToSelection();
    BString count;
    count << fTotal << " packet" << (fTotal == 1 ? "" : "s");
    fCountView->SetText(count.String());
}

void PacketLogView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgPacketsClear:
            for (int32 i = fList->CountItems() - 1; i >= 0; i--)
                delete fList->RemoveItem(i);
            fTotal = 0;
            fCountView->SetText("0 packets");
            break;
        case kMsgPacketsPause:
            break;  // the checkbox state is read directly
        default:
            BView::MessageReceived(message);
            break;
    }
}
