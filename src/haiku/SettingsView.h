// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_SETTINGSVIEW_H
#define MESHY_HAIKU_SETTINGSVIEW_H

#include <View.h>

#include "Engine.h"

class BButton;
class BCheckBox;
class BTextControl;

// Editable radio and node settings.  Each group has its own Apply button, so
// a half-finished edit elsewhere cannot be written to the radio by accident.
class SettingsView : public BView {
public:
    explicit SettingsView(meshy::Engine* engine);
    virtual ~SettingsView();

    virtual void AttachedToWindow();
    virtual void MessageReceived(BMessage* message);

    void Reload();

private:
    void _ApplyRadio();
    void _ApplyName();
    void _ApplyLocation();
    void _ApplyAutoAdd();

    meshy::Engine* fEngine;
    BTextControl* fNameControl;
    BTextControl* fFreqControl;
    BTextControl* fBandwidthControl;
    BTextControl* fSfControl;
    BTextControl* fCrControl;
    BTextControl* fTxPowerControl;
    BTextControl* fLatControl;
    BTextControl* fLonControl;
    BCheckBox* fAutoAddChat;
    BCheckBox* fAutoAddRepeater;
    BCheckBox* fAutoAddRoom;
    BCheckBox* fAutoAddSensor;
    BCheckBox* fAutoAddOverwrite;
    BTextControl* fMaxHopsControl;
};

#endif  // MESHY_HAIKU_SETTINGSVIEW_H
