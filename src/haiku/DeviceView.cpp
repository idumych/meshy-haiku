// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeviceView.h"

#include <Button.h>
#include <GridLayout.h>
#include <GridLayoutBuilder.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <Window.h>

#include <stdio.h>

#include "UiMessages.h"
#include "UiUtils.h"

using namespace meshy;

DeviceView::DeviceView(Engine* engine) : BView("device", B_WILL_DRAW), fEngine(engine)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    BStringView* title = new BStringView("devTitle", "Radio");
    BFont bold(be_bold_font);
    bold.SetSize(bold.Size() * 1.3f);
    title->SetFont(&bold);

    fRefreshButton = new BButton("refresh", "Refresh", new BMessage(kMsgDeviceRefresh));
    fStatsButton = new BButton("stats", "Read statistics", new BMessage(kMsgDeviceStats));

    // Identity and firmware.
    BGridLayout* grid = new BGridLayout(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING);
    BView* gridView = new BView("devGrid", 0);
    gridView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    gridView->SetLayout(grid);

    int row = 0;
    _AddRow(grid, row++, "Name:", "name");
    _AddRow(grid, row++, "Public key:", "pubkey");
    _AddRow(grid, row++, "Model:", "model");
    _AddRow(grid, row++, "Firmware:", "firmware");
    _AddRow(grid, row++, "Protocol:", "protocol");
    _AddRow(grid, row++, "Frequency:", "freq");
    _AddRow(grid, row++, "Bandwidth:", "bw");
    _AddRow(grid, row++, "Spreading factor:", "sf");
    _AddRow(grid, row++, "Coding rate:", "cr");
    _AddRow(grid, row++, "TX power:", "tx");
    _AddRow(grid, row++, "Position:", "position");
    _AddRow(grid, row++, "Battery:", "battery");
    _AddRow(grid, row++, "Storage:", "storage");
    _AddRow(grid, row++, "Contacts:", "contacts");
    _AddRow(grid, row++, "Channels:", "channels");

    BStringView* statsTitle = new BStringView("statsTitle", "Statistics");
    statsTitle->SetFont(be_bold_font);

    BGridLayout* statsGrid = new BGridLayout(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING);
    BView* statsView = new BView("statsGrid", 0);
    statsView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    statsView->SetLayout(statsGrid);

    row = 0;
    _AddRow(statsGrid, row++, "Uptime:", "uptime");
    _AddRow(statsGrid, row++, "Queue:", "queue");
    _AddRow(statsGrid, row++, "Errors:", "errors");
    _AddRow(statsGrid, row++, "Noise floor:", "noise");
    _AddRow(statsGrid, row++, "Last RSSI:", "rssi");
    _AddRow(statsGrid, row++, "Last SNR:", "snr");
    _AddRow(statsGrid, row++, "Airtime TX / RX:", "airtime");
    _AddRow(statsGrid, row++, "Packets recv / sent:", "packets");
    _AddRow(statsGrid, row++, "Flood TX / RX:", "flood");
    _AddRow(statsGrid, row++, "Direct TX / RX:", "direct");

    BView* content = new BView("devContent", 0);
    content->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    BLayoutBuilder::Group<>(content, B_VERTICAL, B_USE_DEFAULT_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .Add(title)
        .Add(gridView)
        .Add(new BSeparatorView(B_HORIZONTAL))
        .Add(statsTitle)
        .Add(statsView)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
            .Add(fRefreshButton)
            .Add(fStatsButton)
            .AddGlue()
        .End()
        .AddGlue()
    .End();

    BScrollView* scroll = new BScrollView("devScroll", content, 0, false, true, B_NO_BORDER);
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0).Add(scroll).End();
}

DeviceView::~DeviceView() {}

BStringView* DeviceView::_AddRow(BGridLayout* grid, int row, const char* label, const char* key)
{
    BStringView* labelView = new BStringView("", label);
    labelView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
    labelView->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT, B_ALIGN_VERTICAL_CENTER));

    BStringView* valueView = new BStringView("", "—");
    valueView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));

    grid->AddView(labelView, 0, row);
    grid->AddView(valueView, 1, row);
    fFields[key] = valueView;
    return valueView;
}

void DeviceView::_Set(const char* key, const BString& value)
{
    std::map<std::string, BStringView*>::iterator it = fFields.find(key);
    if (it != fFields.end())
        it->second->SetText(value.Length() > 0 ? value.String() : "—");
}

void DeviceView::AttachedToWindow()
{
    BView::AttachedToWindow();
    ui::RetargetControls(this, this);
    Reload();
}

void DeviceView::Reload()
{
    DeviceInfo info = fEngine->GetDeviceInfo();

    _Set("name", BString(info.name.c_str()));
    _Set("pubkey", BString(info.public_key.substr(0, 16).c_str()));
    _Set("model", BString(info.model.c_str()));

    BString fw;
    if (!info.ver.empty())
        fw << info.ver.c_str();
    if (!info.fw_build.empty())
        fw << "  (" << info.fw_build.c_str() << ")";
    _Set("firmware", fw);

    BString proto;
    if (info.fw_ver > 0)
        proto << "v" << info.fw_ver;
    _Set("protocol", proto);

    BString freq;
    if (info.radio_freq > 0.0)
        freq.SetToFormat("%.3f MHz", info.radio_freq);
    _Set("freq", freq);

    BString bw;
    if (info.radio_bw > 0.0)
        bw.SetToFormat("%.1f kHz", info.radio_bw);
    _Set("bw", bw);

    BString sf;
    if (info.radio_sf > 0)
        sf << info.radio_sf;
    _Set("sf", sf);

    BString cr;
    if (info.radio_cr > 0)
        cr << "4/" << info.radio_cr;
    _Set("cr", cr);

    BString tx;
    if (info.max_tx_power > 0)
        tx.SetToFormat("%d dBm (max %d)", info.tx_power, info.max_tx_power);
    else if (info.tx_power != 0)
        tx.SetToFormat("%d dBm", info.tx_power);
    _Set("tx", tx);

    BString pos;
    if (info.has_location)
        pos.SetToFormat("%.5f, %.5f", info.adv_lat, info.adv_lon);
    _Set("position", pos);

    BString contacts;
    contacts << static_cast<int>(fEngine->Store().GetContacts().size());
    if (info.max_contacts > 0)
        contacts << " / " << info.max_contacts;
    _Set("contacts", contacts);

    BString channels;
    channels << static_cast<int>(fEngine->Store().GetChannels().size());
    if (info.max_channels > 0)
        channels << " / " << info.max_channels;
    _Set("channels", channels);

    if (info.battery_mv > 0)
        SetBattery(info.battery_mv, static_cast<int>(info.storage_used_kb),
            static_cast<int>(info.storage_total_kb));
}

void DeviceView::SetBattery(int millivolts, int used_kb, int total_kb)
{
    _Set("battery", ui::FormatVoltage(millivolts));
    BString storage;
    if (total_kb > 0) {
        storage << ui::FormatStorage(static_cast<uint32_t>(used_kb)) << " of "
                << ui::FormatStorage(static_cast<uint32_t>(total_kb));
        int percent = static_cast<int>(100.0 * used_kb / total_kb);
        storage << "  (" << percent << "%)";
    }
    _Set("storage", storage);
}

void DeviceView::HandleStats(BMessage* message)
{
    int32 sub = 0;
    message->FindInt32("sub_type", &sub);

    if (sub == STATS_TYPE_CORE) {
        int32 uptime = 0, errors = 0, queue = 0, mv = 0;
        message->FindInt32("uptime_secs", &uptime);
        message->FindInt32("errors", &errors);
        message->FindInt32("queue_len", &queue);
        message->FindInt32("battery_mv", &mv);
        _Set("uptime", ui::FormatDuration(static_cast<uint32_t>(uptime)));
        BString e;
        e << errors;
        _Set("errors", e);
        BString q;
        q << queue;
        _Set("queue", q);
        if (mv > 0)
            _Set("battery", ui::FormatVoltage(mv));
    } else if (sub == STATS_TYPE_RADIO) {
        int32 noise = 0, rssi = 0, tx = 0, rx = 0;
        double snr = 0.0;
        message->FindInt32("noise_floor", &noise);
        message->FindInt32("last_rssi", &rssi);
        message->FindDouble("last_snr", &snr);
        message->FindInt32("tx_air_secs", &tx);
        message->FindInt32("rx_air_secs", &rx);
        BString n;
        n << noise << " dBm";
        _Set("noise", n);
        BString r;
        r << rssi << " dBm";
        _Set("rssi", r);
        _Set("snr", ui::FormatSnr(snr));
        BString air;
        air << ui::FormatDuration(static_cast<uint32_t>(tx)) << " / "
            << ui::FormatDuration(static_cast<uint32_t>(rx));
        _Set("airtime", air);
    } else if (sub == STATS_TYPE_PACKETS) {
        int32 recv = 0, sent = 0, ftx = 0, frx = 0, dtx = 0, drx = 0;
        message->FindInt32("recv", &recv);
        message->FindInt32("sent", &sent);
        message->FindInt32("flood_tx", &ftx);
        message->FindInt32("flood_rx", &frx);
        message->FindInt32("direct_tx", &dtx);
        message->FindInt32("direct_rx", &drx);
        BString p;
        p << recv << " / " << sent;
        _Set("packets", p);
        BString f;
        f << ftx << " / " << frx;
        _Set("flood", f);
        BString d;
        d << dtx << " / " << drx;
        _Set("direct", d);
    }
}

void DeviceView::HandleTrace(BMessage* message)
{
    // Trace results are informational; summarise them in the status strip.
    BString s("Trace: ");
    const char* hop = NULL;
    int32 i = 0;
    while (message->FindString("hop", i, &hop) == B_OK) {
        if (i > 0)
            s << " → ";
        s << hop;
        i++;
    }
    if (i == 0)
        s << "no hops reported";
    if (Window() != NULL) {
        BMessage note(kEngineToast);
        note.AddString("text", s.String());
        Window()->PostMessage(&note);
    }
}

void DeviceView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgDeviceRefresh:
            fEngine->RequestBattery();
            Reload();
            break;
        case kMsgDeviceStats:
            // The three blocks are separate requests.
            fEngine->RequestStats(STATS_TYPE_CORE);
            fEngine->RequestStats(STATS_TYPE_RADIO);
            fEngine->RequestStats(STATS_TYPE_PACKETS);
            break;
        default:
            BView::MessageReceived(message);
            break;
    }
}
