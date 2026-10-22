// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adapts the engine's listener interface to Haiku's message passing.  Every
// callback arrives on the engine worker thread and is converted into a
// BMessage posted to the target looper, so all UI work happens on the window
// thread without any locking.

#ifndef MESHY_HAIKU_ENGINEBRIDGE_H
#define MESHY_HAIKU_ENGINEBRIDGE_H

#include <Messenger.h>

#include "Engine.h"

class EngineBridge : public meshy::EngineListener {
public:
    explicit EngineBridge(const BMessenger& target);
    virtual ~EngineBridge();

    virtual void OnConnectionState(meshy::ConnectionState state);
    virtual void OnSyncProgress(const meshy::SyncProgress& p);
    virtual void OnToast(const std::string& text);
    virtual void OnError(const std::string& text);
    virtual void OnDeviceInfoChanged(const meshy::DeviceInfo& info);
    virtual void OnContactsChanged();
    virtual void OnChannelsChanged();
    virtual void OnDiscoveredChanged();
    virtual void OnMessage(const std::string& contact_key_hex, int64_t message_id);
    virtual void OnMessageStatus(int64_t message_id, int status);
    virtual void OnChannelMessage(int channel_index, int64_t message_id);
    virtual void OnCliReply(const std::string& text);
    virtual void OnRemoteCliReply(const std::string& contact_key_hex, const std::string& text);
    virtual void OnLoginResult(const std::string& contact_key_hex, bool success);
    virtual void OnStatusResponse(const std::string& contact_key_hex,
        const meshy::StatusResponse& s);
    virtual void OnTelemetry(const std::string& contact_key_hex, const meshy::ByteBuffer& lpp);
    virtual void OnTraceResult(const meshy::TraceResponse& t);
    virtual void OnStats(const meshy::StatsResponse& s);
    virtual void OnBattery(int millivolts, uint32_t used_kb, uint32_t total_kb);
    virtual void OnRawPacket(const meshy::LogPacket& pkt);

private:
    BMessenger fTarget;
};

#endif  // MESHY_HAIKU_ENGINEBRIDGE_H
