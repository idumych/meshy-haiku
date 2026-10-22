// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MeshyApp.h"

#include <Alert.h>
#include <Messenger.h>

#include "EngineBridge.h"
#include "MainWindow.h"

const char* kAppSignature = "application/x-vnd.Meshy-Haiku";

MeshyApp::MeshyApp() : BApplication(kAppSignature), fBridge(NULL), fWindow(NULL) {}

MeshyApp::~MeshyApp()
{
    // Detach the listener before the bridge dies, so a late engine callback
    // cannot reach freed memory.
    fEngine.SetListener(NULL);
    fEngine.Stop();
    delete fBridge;
}

void MeshyApp::ReadyToRun()
{
    if (!fEngine.Start()) {
        BString text("Meshy could not open its database:\n");
        text << fEngine.Store().LastError().c_str();
        BAlert* alert = new BAlert("Meshy", text.String(), "Quit", NULL, NULL, B_WIDTH_AS_USUAL,
            B_STOP_ALERT);
        alert->SetShortcut(0, B_ESCAPE);
        alert->Go();
        PostMessage(B_QUIT_REQUESTED);
        return;
    }

    fWindow = new MainWindow(&fEngine);
    fBridge = new EngineBridge(BMessenger(fWindow));
    fEngine.SetListener(fBridge);
    fWindow->Show();
}

void MeshyApp::AboutRequested()
{
    BAlert* alert = new BAlert("About Meshy",
        "Meshy for Haiku\n\n"
        "A native client for MeshCore mesh radios.\n\n"
        "Ported from Meshy by Jiri Eischmann and contributors.\n"
        "Licensed under the GNU GPL v3 or later.",
        "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
    alert->SetShortcut(0, B_ESCAPE);
    alert->Go();
}

bool MeshyApp::QuitRequested()
{
    fEngine.SetListener(NULL);
    fEngine.Stop();
    return BApplication::QuitRequested();
}

int main()
{
    MeshyApp app;
    app.Run();
    return 0;
}
