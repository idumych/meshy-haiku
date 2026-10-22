// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small formatting helpers shared by the Haiku views.

#ifndef MESHY_HAIKU_UIUTILS_H
#define MESHY_HAIKU_UIUTILS_H

#include <String.h>

#include <string>

#include "Models.h"

class BHandler;
class BView;

namespace ui {

inline BString Str(const std::string& s) { return BString(s.c_str()); }
inline std::string Std(const BString& s) { return std::string(s.String()); }

// "14:32" for today, "Mon 14:32" within the past week, else "2026-10-16 14:32".
BString FormatTimestamp(double unix_time);
// Clock time only, for message bubbles.
BString FormatClock(double unix_time);
// "3 minutes ago", "2 days ago", "never".
BString FormatRelative(double unix_time);
// "2 h 14 m", for uptime.
BString FormatDuration(uint32_t seconds);
// "-7.2 dB"
BString FormatSnr(double snr);
// "4.10 V" from millivolts.
BString FormatVoltage(int millivolts);
// "1.2 MB" from kilobytes.
BString FormatStorage(uint32_t kb);
// "Flood" for -1, otherwise "3 hops".
BString FormatPath(int path_length);

BString ContactTypeName(int type);
BString MessageStatusName(int status);
BString ConnectionStateName(int state);

// Points every BInvoker descendant of `root` (buttons, checkboxes, text
// controls) at `target`.  Layout builders nest controls inside anonymous
// group views, so walking only the direct children misses most of them.
void RetargetControls(BView* root, BHandler* target);

}  // namespace ui

#endif  // MESHY_HAIKU_UIUTILS_H
