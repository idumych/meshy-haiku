// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_SENSORVIEW_H
#define MESHY_HAIKU_SENSORVIEW_H

#include <View.h>

#include <string>

#include "Engine.h"

class BButton;
class BListView;
class BStringView;

// Shows the most recent Cayenne LPP telemetry from a sensor node.
class SensorView : public BView {
public:
    explicit SensorView(meshy::Engine* engine);
    virtual ~SensorView();

    virtual void AttachedToWindow();
    virtual void MessageReceived(BMessage* message);

    void ShowContact(const std::string& contact_key_hex);
    void HandleTelemetry(BMessage* message);

private:
    void _UpdateHeader();

    meshy::Engine* fEngine;
    std::string fContactKey;
    BStringView* fHeaderView;
    BStringView* fStateView;
    BListView* fList;
    BButton* fRefreshButton;
};

#endif  // MESHY_HAIKU_SENSORVIEW_H
