// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsView.h"

#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <stdlib.h>

#include "UiMessages.h"
#include "UiUtils.h"

using namespace meshy;

SettingsView::SettingsView(Engine* engine) : BView("settings", B_WILL_DRAW), fEngine(engine)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    BStringView* title = new BStringView("setTitle", "Settings");
    BFont bold(be_bold_font);
    bold.SetSize(bold.Size() * 1.3f);
    title->SetFont(&bold);

    // ── Identity ──
    BStringView* idTitle = new BStringView("idTitle", "Node");
    idTitle->SetFont(be_bold_font);
    fNameControl = new BTextControl("name", "Name:", "", NULL);
    BButton* applyName = new BButton("applyName", "Apply name", new BMessage(kMsgApplyName));

    // ── Radio ──
    BStringView* radioTitle = new BStringView("radioTitle", "Radio");
    radioTitle->SetFont(be_bold_font);
    fFreqControl = new BTextControl("freq", "Frequency (MHz):", "", NULL);
    fBandwidthControl = new BTextControl("bw", "Bandwidth (kHz):", "", NULL);
    fSfControl = new BTextControl("sf", "Spreading factor:", "", NULL);
    fCrControl = new BTextControl("cr", "Coding rate (4/x):", "", NULL);
    fTxPowerControl = new BTextControl("tx", "TX power (dBm):", "", NULL);
    BButton* applyRadio = new BButton("applyRadio", "Apply radio settings",
        new BMessage(kMsgApplyRadio));
    BButton* applyTx = new BButton("applyTx", "Apply TX power", new BMessage(kMsgApplyTxPower));

    BStringView* radioWarn = new BStringView("radioWarn",
        "Every node on a mesh must share the same frequency, bandwidth, spreading factor and "
        "coding rate.");
    radioWarn->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

    // ── Position ──
    BStringView* posTitle = new BStringView("posTitle", "Position");
    posTitle->SetFont(be_bold_font);
    fLatControl = new BTextControl("lat", "Latitude:", "", NULL);
    fLonControl = new BTextControl("lon", "Longitude:", "", NULL);
    BButton* applyLocation = new BButton("applyLoc", "Apply position",
        new BMessage(kMsgApplyLocation));

    // ── Automatic contacts ──
    BStringView* autoTitle = new BStringView("autoTitle", "Add contacts automatically");
    autoTitle->SetFont(be_bold_font);
    fAutoAddChat = new BCheckBox("aaChat", "Chat nodes", NULL);
    fAutoAddRepeater = new BCheckBox("aaRep", "Repeaters", NULL);
    fAutoAddRoom = new BCheckBox("aaRoom", "Room servers", NULL);
    fAutoAddSensor = new BCheckBox("aaSensor", "Sensors", NULL);
    fAutoAddOverwrite = new BCheckBox("aaOver", "Overwrite the oldest when full", NULL);
    fMaxHopsControl = new BTextControl("maxHops", "Maximum hops:", "0", NULL);
    BButton* applyAutoAdd = new BButton("applyAuto", "Apply", new BMessage(kMsgApplyAutoAdd));

    BView* content = new BView("setContent", 0);
    content->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    BLayoutBuilder::Group<>(content, B_VERTICAL, B_USE_DEFAULT_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .Add(title)

        .Add(idTitle)
        .Add(fNameControl)
        .AddGroup(B_HORIZONTAL).Add(applyName).AddGlue().End()
        .Add(new BSeparatorView(B_HORIZONTAL))

        .Add(radioTitle)
        .Add(fFreqControl)
        .Add(fBandwidthControl)
        .Add(fSfControl)
        .Add(fCrControl)
        .AddGroup(B_HORIZONTAL).Add(applyRadio).AddGlue().End()
        .Add(fTxPowerControl)
        .AddGroup(B_HORIZONTAL).Add(applyTx).AddGlue().End()
        .Add(radioWarn)
        .Add(new BSeparatorView(B_HORIZONTAL))

        .Add(posTitle)
        .Add(fLatControl)
        .Add(fLonControl)
        .AddGroup(B_HORIZONTAL).Add(applyLocation).AddGlue().End()
        .Add(new BSeparatorView(B_HORIZONTAL))

        .Add(autoTitle)
        .Add(fAutoAddChat)
        .Add(fAutoAddRepeater)
        .Add(fAutoAddRoom)
        .Add(fAutoAddSensor)
        .Add(fAutoAddOverwrite)
        .Add(fMaxHopsControl)
        .AddGroup(B_HORIZONTAL).Add(applyAutoAdd).AddGlue().End()
        .AddGlue()
    .End();

    BScrollView* scroll = new BScrollView("setScroll", content, 0, false, true, B_NO_BORDER);
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0).Add(scroll).End();
}

SettingsView::~SettingsView() {}

void SettingsView::AttachedToWindow()
{
    BView::AttachedToWindow();
    // Layout builders nest controls, so retarget the whole subtree.
    ui::RetargetControls(this, this);
    Reload();
}

void SettingsView::Reload()
{
    DeviceInfo info = fEngine->GetDeviceInfo();

    fNameControl->SetText(info.name.c_str());

    BString s;
    if (info.radio_freq > 0.0) {
        s.SetToFormat("%.3f", info.radio_freq);
        fFreqControl->SetText(s.String());
    }
    if (info.radio_bw > 0.0) {
        s.SetToFormat("%.1f", info.radio_bw);
        fBandwidthControl->SetText(s.String());
    }
    if (info.radio_sf > 0) {
        s.SetToFormat("%d", info.radio_sf);
        fSfControl->SetText(s.String());
    }
    if (info.radio_cr > 0) {
        s.SetToFormat("%d", info.radio_cr);
        fCrControl->SetText(s.String());
    }
    s.SetToFormat("%d", info.tx_power);
    fTxPowerControl->SetText(s.String());

    if (info.has_location) {
        s.SetToFormat("%.6f", info.adv_lat);
        fLatControl->SetText(s.String());
        s.SetToFormat("%.6f", info.adv_lon);
        fLonControl->SetText(s.String());
    }
}

void SettingsView::_ApplyName()
{
    BString name(fNameControl->Text());
    name.Trim();
    if (name.Length() == 0)
        return;
    fEngine->SetDeviceName(ui::Std(name));
}

void SettingsView::_ApplyRadio()
{
    double freq = atof(fFreqControl->Text());
    double bw = atof(fBandwidthControl->Text());
    int sf = atoi(fSfControl->Text());
    int cr = atoi(fCrControl->Text());

    if (freq <= 0.0 || bw <= 0.0 || sf < 5 || sf > 12 || cr < 5 || cr > 8) {
        BAlert* alert = new BAlert("Meshy",
            "Check the radio values: frequency and bandwidth must be positive, the spreading "
            "factor 5–12, and the coding rate 5–8.",
            "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
        alert->SetShortcut(0, B_ESCAPE);
        alert->Go();
        return;
    }

    BAlert* confirm = new BAlert("Meshy",
        "Changing the radio settings will drop any node that is not reconfigured to match. "
        "Continue?",
        "Cancel", "Apply", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
    confirm->SetShortcut(0, B_ESCAPE);
    if (confirm->Go() != 1)
        return;

    // The firmware takes kHz for both.
    fEngine->SetRadioParams(static_cast<uint32_t>(freq * 1000.0 + 0.5),
        static_cast<uint32_t>(bw + 0.5), static_cast<uint8_t>(sf), static_cast<uint8_t>(cr));
}

void SettingsView::_ApplyLocation()
{
    double lat = atof(fLatControl->Text());
    double lon = atof(fLonControl->Text());
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        BAlert* alert = new BAlert("Meshy", "Latitude must be −90 to 90 and longitude −180 to 180.",
            "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
        alert->SetShortcut(0, B_ESCAPE);
        alert->Go();
        return;
    }
    fEngine->SetDeviceLocation(lat, lon);
}

void SettingsView::_ApplyAutoAdd()
{
    int hops = atoi(fMaxHopsControl->Text());
    if (hops < 0)
        hops = 0;
    if (hops > 64)
        hops = 64;
    fEngine->SetAutoAddConfig(fAutoAddChat->Value() == B_CONTROL_ON,
        fAutoAddRepeater->Value() == B_CONTROL_ON, fAutoAddRoom->Value() == B_CONTROL_ON,
        fAutoAddSensor->Value() == B_CONTROL_ON, fAutoAddOverwrite->Value() == B_CONTROL_ON,
        static_cast<uint8_t>(hops));
}

void SettingsView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgApplyName:
            _ApplyName();
            break;
        case kMsgApplyRadio:
            _ApplyRadio();
            break;
        case kMsgApplyTxPower:
            fEngine->SetTxPower(atoi(fTxPowerControl->Text()));
            break;
        case kMsgApplyLocation:
            _ApplyLocation();
            break;
        case kMsgApplyAutoAdd:
            _ApplyAutoAdd();
            break;
        default:
            BView::MessageReceived(message);
            break;
    }
}
