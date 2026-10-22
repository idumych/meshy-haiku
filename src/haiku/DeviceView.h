// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_DEVICEVIEW_H
#define MESHY_HAIKU_DEVICEVIEW_H

#include <View.h>

#include <map>
#include <string>

#include "Engine.h"

class BButton;
class BStringView;

// Read-only overview of the connected radio: identity, firmware, radio
// parameters, battery, storage and the three stats blocks.
class DeviceView : public BView {
public:
    explicit DeviceView(meshy::Engine* engine);
    virtual ~DeviceView();

    virtual void AttachedToWindow();
    virtual void MessageReceived(BMessage* message);

    void Reload();
    void SetBattery(int millivolts, int used_kb, int total_kb);
    void HandleStats(BMessage* message);
    void HandleTrace(BMessage* message);

private:
    // Adds a "Label: value" row and remembers the value view by key.
    BStringView* _AddRow(class BGridLayout* grid, int row, const char* label, const char* key);
    void _Set(const char* key, const BString& value);

    meshy::Engine* fEngine;
    std::map<std::string, BStringView*> fFields;
    BButton* fRefreshButton;
    BButton* fStatsButton;
};

#endif  // MESHY_HAIKU_DEVICEVIEW_H
