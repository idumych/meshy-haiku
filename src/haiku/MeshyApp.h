// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_MESHYAPP_H
#define MESHY_HAIKU_MESHYAPP_H

#include <Application.h>

#include "Engine.h"

class EngineBridge;
class MainWindow;

extern const char* kAppSignature;

class MeshyApp : public BApplication {
public:
    MeshyApp();
    virtual ~MeshyApp();

    virtual void ReadyToRun();
    virtual void AboutRequested();
    virtual bool QuitRequested();

private:
    meshy::Engine fEngine;
    EngineBridge* fBridge;
    MainWindow* fWindow;
};

#endif  // MESHY_HAIKU_MESHYAPP_H
