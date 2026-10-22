// Definitions for the mock globals, so the type-check can link.
#include <Font.h>
#include <InterfaceDefs.h>
#include <ControlLook.h>
#include <Application.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
static BFont gPlain, gBold, gFixed;
const BFont* be_plain_font = &gPlain;
const BFont* be_bold_font = &gBold;
const BFont* be_fixed_font = &gFixed;
static BControlLook gLook;
BControlLook* be_control_look = &gLook;
BApplication* be_app = 0;
static BRoster gRoster;
BRoster* be_roster = &gRoster;
rgb_color ui_color(color_which) { rgb_color c = {0,0,0,255}; return c; }
rgb_color tint_color(rgb_color c, float) { return c; }
status_t find_directory(directory_which, BPath*) { return B_OK; }
