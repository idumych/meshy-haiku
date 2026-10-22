// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SensorView.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringView.h>
#include <Window.h>

#include <stdio.h>

#include "Protocol.h"
#include "UiMessages.h"
#include "UiUtils.h"

using namespace meshy;

SensorView::SensorView(Engine* engine) : BView("sensor", B_WILL_DRAW), fEngine(engine)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    fHeaderView = new BStringView("senHeader", "No sensor selected");
    BFont bold(be_bold_font);
    bold.SetSize(bold.Size() * 1.2f);
    fHeaderView->SetFont(&bold);

    fStateView = new BStringView("senState", "");
    fStateView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

    fList = new BListView("telemetry", B_SINGLE_SELECTION_LIST);
    BScrollView* scroll = new BScrollView("senScroll", fList, 0, false, true, B_FANCY_BORDER);

    fRefreshButton = new BButton("refresh", "Request telemetry", new BMessage(kMsgSensorRefresh));

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .Add(fHeaderView)
        .Add(fStateView)
        .Add(scroll, 1.0f)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
            .Add(fRefreshButton)
            .AddGlue()
        .End()
    .End();
}

SensorView::~SensorView() {}

void SensorView::AttachedToWindow()
{
    BView::AttachedToWindow();
    ui::RetargetControls(this, this);
    _UpdateHeader();
}

void SensorView::ShowContact(const std::string& contact_key_hex)
{
    if (contact_key_hex != fContactKey) {
        fContactKey = contact_key_hex;
        fList->MakeEmpty();
    }
    _UpdateHeader();
    // Ask straight away; readings are not pushed unsolicited.
    if (!fContactKey.empty())
        fEngine->RequestTelemetry(fContactKey);
}

void SensorView::_UpdateHeader()
{
    if (fContactKey.empty()) {
        fHeaderView->SetText("No sensor selected");
        fStateView->SetText("Pick a sensor from the contact list.");
        fRefreshButton->SetEnabled(false);
        return;
    }
    Contact c;
    if (fEngine->Store().GetContact(fContactKey, &c)) {
        fHeaderView->SetText(c.DisplayName().c_str());
        BString s;
        s << ui::ContactTypeName(c.type) << " · " << ui::FormatPath(c.path_length);
        if (HasTime(c.last_seen))
            s << " · heard " << ui::FormatRelative(c.last_seen);
        fStateView->SetText(s.String());
    }
    fRefreshButton->SetEnabled(true);
}

void SensorView::HandleTelemetry(BMessage* message)
{
    const char* contact = NULL;
    message->FindString("contact", &contact);
    if (contact != NULL && !fContactKey.empty() && fContactKey != contact)
        return;

    const void* data = NULL;
    ssize_t size = 0;
    if (message->FindData("lpp", B_RAW_TYPE, &data, &size) != B_OK || data == NULL || size <= 0) {
        fList->MakeEmpty();
        fList->AddItem(new BStringItem("No readings in this response."));
        return;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    ByteBuffer lpp(bytes, bytes + size);
    std::vector<LppReading> readings = ParseCayenneLpp(lpp);

    fList->MakeEmpty();
    if (readings.empty()) {
        fList->AddItem(new BStringItem("The telemetry payload could not be decoded."));
        return;
    }

    for (size_t i = 0; i < readings.size(); i++) {
        const LppReading& r = readings[i];
        char line[256];
        if (r.is_vector) {
            if (r.type == LPP_GPS)
                snprintf(line, sizeof(line), "ch %u  %s:  %.5f, %.5f  (%.1f m)", r.channel,
                    r.name, r.x, r.y, r.z);
            else if (r.type == LPP_COLOUR)
                snprintf(line, sizeof(line), "ch %u  %s:  R %.0f  G %.0f  B %.0f", r.channel,
                    r.name, r.x, r.y, r.z);
            else
                snprintf(line, sizeof(line), "ch %u  %s:  x %.3f  y %.3f  z %.3f %s", r.channel,
                    r.name, r.x, r.y, r.z, r.unit);
        } else {
            snprintf(line, sizeof(line), "ch %u  %s:  %.3f %s", r.channel, r.name, r.value,
                r.unit);
        }
        fList->AddItem(new BStringItem(line));
    }

    BString s;
    s << static_cast<int>(readings.size()) << " reading"
      << (readings.size() == 1 ? "" : "s") << " at " << ui::FormatClock(time(NULL));
    fStateView->SetText(s.String());
}

void SensorView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgSensorRefresh:
            if (!fContactKey.empty())
                fEngine->RequestTelemetry(fContactKey);
            break;
        default:
            BView::MessageReceived(message);
            break;
    }
}
