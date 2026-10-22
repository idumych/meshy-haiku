// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UiUtils.h"

#include <Control.h>
#include <Handler.h>
#include <Invoker.h>
#include <View.h>

#include <stdio.h>
#include <time.h>

#include "Transport.h"

namespace ui {

BString FormatTimestamp(double unix_time)
{
    if (!meshy::HasTime(unix_time))
        return BString("—");
    time_t t = static_cast<time_t>(unix_time);
    struct tm tm_val;
    localtime_r(&t, &tm_val);

    time_t now = time(NULL);
    struct tm now_tm;
    localtime_r(&now, &now_tm);

    char buf[64];
    if (tm_val.tm_year == now_tm.tm_year && tm_val.tm_yday == now_tm.tm_yday)
        strftime(buf, sizeof(buf), "%H:%M", &tm_val);
    else if (now - t < 7 * 24 * 3600 && now >= t)
        strftime(buf, sizeof(buf), "%a %H:%M", &tm_val);
    else
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_val);
    return BString(buf);
}

BString FormatClock(double unix_time)
{
    if (!meshy::HasTime(unix_time))
        return BString("");
    time_t t = static_cast<time_t>(unix_time);
    struct tm tm_val;
    localtime_r(&t, &tm_val);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M", &tm_val);
    return BString(buf);
}

BString FormatRelative(double unix_time)
{
    if (!meshy::HasTime(unix_time))
        return BString("never");
    time_t now = time(NULL);
    long diff = static_cast<long>(now - static_cast<time_t>(unix_time));
    if (diff < 0)
        diff = 0;

    char buf[64];
    if (diff < 60)
        return BString("just now");
    if (diff < 3600) {
        long m = diff / 60;
        snprintf(buf, sizeof(buf), "%ld minute%s ago", m, m == 1 ? "" : "s");
    } else if (diff < 86400) {
        long h = diff / 3600;
        snprintf(buf, sizeof(buf), "%ld hour%s ago", h, h == 1 ? "" : "s");
    } else {
        long d = diff / 86400;
        snprintf(buf, sizeof(buf), "%ld day%s ago", d, d == 1 ? "" : "s");
    }
    return BString(buf);
}

BString FormatDuration(uint32_t seconds)
{
    char buf[64];
    if (seconds < 60)
        snprintf(buf, sizeof(buf), "%u s", seconds);
    else if (seconds < 3600)
        snprintf(buf, sizeof(buf), "%u m", seconds / 60);
    else if (seconds < 86400)
        snprintf(buf, sizeof(buf), "%u h %u m", seconds / 3600, (seconds % 3600) / 60);
    else
        snprintf(buf, sizeof(buf), "%u d %u h", seconds / 86400, (seconds % 86400) / 3600);
    return BString(buf);
}

BString FormatSnr(double snr)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f dB", snr);
    return BString(buf);
}

BString FormatVoltage(int millivolts)
{
    if (millivolts <= 0)
        return BString("—");
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f V", millivolts / 1000.0);
    return BString(buf);
}

BString FormatStorage(uint32_t kb)
{
    char buf[32];
    if (kb < 1024)
        snprintf(buf, sizeof(buf), "%u KB", kb);
    else
        snprintf(buf, sizeof(buf), "%.1f MB", kb / 1024.0);
    return BString(buf);
}

BString FormatPath(int path_length)
{
    if (path_length < 0)
        return BString("Flood");
    if (path_length == 0)
        return BString("Direct");
    char buf[32];
    snprintf(buf, sizeof(buf), "%d hop%s", path_length, path_length == 1 ? "" : "s");
    return BString(buf);
}

BString ContactTypeName(int type)
{
    switch (type) {
        case meshy::CONTACT_CHAT: return BString("Chat");
        case meshy::CONTACT_REPEATER: return BString("Repeater");
        case meshy::CONTACT_ROOM: return BString("Room");
        case meshy::CONTACT_SENSOR: return BString("Sensor");
        default: return BString("Unknown");
    }
}

BString MessageStatusName(int status)
{
    switch (status) {
        case meshy::MSG_PENDING: return BString("Sending…");
        case meshy::MSG_SENT: return BString("Sent");
        case meshy::MSG_DELIVERED: return BString("Delivered");
        case meshy::MSG_FAILED: return BString("Failed");
        default: return BString("");
    }
}

BString ConnectionStateName(int state)
{
    switch (state) {
        case meshy::CONN_DISCONNECTED: return BString("Disconnected");
        case meshy::CONN_SCANNING: return BString("Scanning");
        case meshy::CONN_CONNECTING: return BString("Connecting…");
        case meshy::CONN_CONNECTED: return BString("Connected");
        case meshy::CONN_DISCONNECTING: return BString("Disconnecting…");
        default: return BString("Unknown");
    }
}

void RetargetControls(BView* root, BHandler* target)
{
    if (root == NULL)
        return;
    for (int32 i = 0; i < root->CountChildren(); i++) {
        BView* child = root->ChildAt(i);
        if (child == NULL)
            continue;
        BInvoker* invoker = dynamic_cast<BInvoker*>(child);
        if (invoker != NULL && invoker->Message() != NULL)
            invoker->SetTarget(target);
        RetargetControls(child, target);
    }
}

}  // namespace ui
