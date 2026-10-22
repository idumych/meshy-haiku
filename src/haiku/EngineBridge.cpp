// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EngineBridge.h"

#include <Message.h>

#include "UiMessages.h"

using namespace meshy;

EngineBridge::EngineBridge(const BMessenger& target) : fTarget(target) {}

EngineBridge::~EngineBridge() {}

void EngineBridge::OnConnectionState(ConnectionState state)
{
    BMessage m(kEngineConnectionState);
    m.AddInt32("state", static_cast<int32>(state));
    fTarget.SendMessage(&m);
}

void EngineBridge::OnSyncProgress(const SyncProgress& p)
{
    BMessage m(kEngineSyncProgress);
    m.AddBool("active", p.active);
    m.AddInt32("done", p.contacts_done);
    m.AddInt32("total", p.contacts_total);
    m.AddBool("channels", p.channels);
    m.AddBool("messages", p.messages);
    fTarget.SendMessage(&m);
}

void EngineBridge::OnToast(const std::string& text)
{
    BMessage m(kEngineToast);
    m.AddString("text", text.c_str());
    fTarget.SendMessage(&m);
}

void EngineBridge::OnError(const std::string& text)
{
    BMessage m(kEngineError);
    m.AddString("text", text.c_str());
    fTarget.SendMessage(&m);
}

void EngineBridge::OnDeviceInfoChanged(const DeviceInfo& info)
{
    // The window re-reads the full struct from the engine; this is only a
    // nudge, so the message stays small.
    BMessage m(kEngineDeviceInfo);
    m.AddString("name", info.name.c_str());
    fTarget.SendMessage(&m);
}

void EngineBridge::OnContactsChanged()
{
    BMessage m(kEngineContactsChanged);
    fTarget.SendMessage(&m);
}

void EngineBridge::OnChannelsChanged()
{
    BMessage m(kEngineChannelsChanged);
    fTarget.SendMessage(&m);
}

void EngineBridge::OnDiscoveredChanged()
{
    BMessage m(kEngineDiscoveredChanged);
    fTarget.SendMessage(&m);
}

void EngineBridge::OnMessage(const std::string& contact_key_hex, int64_t message_id)
{
    BMessage m(kEngineMessage);
    m.AddString("contact", contact_key_hex.c_str());
    m.AddInt64("id", message_id);
    fTarget.SendMessage(&m);
}

void EngineBridge::OnMessageStatus(int64_t message_id, int status)
{
    BMessage m(kEngineMessageStatus);
    m.AddInt64("id", message_id);
    m.AddInt32("status", status);
    fTarget.SendMessage(&m);
}

void EngineBridge::OnChannelMessage(int channel_index, int64_t message_id)
{
    BMessage m(kEngineChannelMessage);
    m.AddInt32("channel", channel_index);
    m.AddInt64("id", message_id);
    fTarget.SendMessage(&m);
}

void EngineBridge::OnCliReply(const std::string& text)
{
    BMessage m(kEngineCliReply);
    m.AddString("text", text.c_str());
    fTarget.SendMessage(&m);
}

void EngineBridge::OnRemoteCliReply(const std::string& contact_key_hex, const std::string& text)
{
    BMessage m(kEngineRemoteCliReply);
    m.AddString("contact", contact_key_hex.c_str());
    m.AddString("text", text.c_str());
    fTarget.SendMessage(&m);
}

void EngineBridge::OnLoginResult(const std::string& contact_key_hex, bool success)
{
    BMessage m(kEngineLoginResult);
    m.AddString("contact", contact_key_hex.c_str());
    m.AddBool("success", success);
    fTarget.SendMessage(&m);
}

void EngineBridge::OnStatusResponse(const std::string& contact_key_hex, const StatusResponse& s)
{
    BMessage m(kEngineStatusResponse);
    m.AddString("contact", contact_key_hex.c_str());
    m.AddInt32("battery_mv", s.battery_mv);
    m.AddInt32("queue_len", s.queue_len);
    m.AddInt32("noise_floor", s.noise_floor);
    m.AddInt32("last_rssi", s.last_rssi);
    m.AddInt32("packets_recv", static_cast<int32>(s.packets_recv));
    m.AddInt32("packets_sent", static_cast<int32>(s.packets_sent));
    m.AddInt32("uptime_secs", static_cast<int32>(s.uptime_secs));
    m.AddInt32("tx_air_secs", static_cast<int32>(s.tx_air_secs));
    m.AddInt32("rx_air_secs", static_cast<int32>(s.rx_air_secs));
    m.AddInt32("flood_tx", static_cast<int32>(s.flood_tx));
    m.AddInt32("direct_tx", static_cast<int32>(s.direct_tx));
    m.AddInt32("flood_rx", static_cast<int32>(s.flood_rx));
    m.AddInt32("direct_rx", static_cast<int32>(s.direct_rx));
    m.AddInt32("error_events", s.error_events);
    m.AddDouble("last_snr", s.last_snr);
    fTarget.SendMessage(&m);
}

void EngineBridge::OnTelemetry(const std::string& contact_key_hex, const ByteBuffer& lpp)
{
    BMessage m(kEngineTelemetry);
    m.AddString("contact", contact_key_hex.c_str());
    if (!lpp.empty())
        m.AddData("lpp", B_RAW_TYPE, lpp.data(), lpp.size());
    fTarget.SendMessage(&m);
}

void EngineBridge::OnTraceResult(const TraceResponse& t)
{
    BMessage m(kEngineTraceResult);
    m.AddInt32("hash_size", t.path_hash_size);
    for (size_t i = 0; i < t.path_hops.size(); i++)
        m.AddString("hop", ToHex(t.path_hops[i]).c_str());
    for (size_t i = 0; i < t.snr_values.size(); i++)
        m.AddDouble("snr", t.snr_values[i]);
    fTarget.SendMessage(&m);
}

void EngineBridge::OnStats(const StatsResponse& s)
{
    BMessage m(kEngineStats);
    m.AddInt32("sub_type", s.sub_type);
    m.AddInt32("battery_mv", s.battery_mv);
    m.AddInt32("uptime_secs", static_cast<int32>(s.uptime_secs));
    m.AddInt32("errors", s.errors);
    m.AddInt32("queue_len", s.queue_len);
    m.AddInt32("noise_floor", s.noise_floor);
    m.AddInt32("last_rssi", s.last_rssi);
    m.AddDouble("last_snr", s.last_snr);
    m.AddInt32("tx_air_secs", static_cast<int32>(s.tx_air_secs));
    m.AddInt32("rx_air_secs", static_cast<int32>(s.rx_air_secs));
    m.AddInt32("recv", static_cast<int32>(s.recv));
    m.AddInt32("sent", static_cast<int32>(s.sent));
    m.AddInt32("flood_tx", static_cast<int32>(s.flood_tx));
    m.AddInt32("direct_tx", static_cast<int32>(s.direct_tx));
    m.AddInt32("flood_rx", static_cast<int32>(s.flood_rx));
    m.AddInt32("direct_rx", static_cast<int32>(s.direct_rx));
    fTarget.SendMessage(&m);
}

void EngineBridge::OnBattery(int millivolts, uint32_t used_kb, uint32_t total_kb)
{
    BMessage m(kEngineBattery);
    m.AddInt32("mv", millivolts);
    m.AddInt32("used_kb", static_cast<int32>(used_kb));
    m.AddInt32("total_kb", static_cast<int32>(total_kb));
    fTarget.SendMessage(&m);
}

void EngineBridge::OnRawPacket(const LogPacket& pkt)
{
    BMessage m(kEngineRawPacket);
    m.AddInt32("payload_type", pkt.payload_type);
    m.AddInt32("route_type", pkt.route_type);
    m.AddString("hash", pkt.pkt_hash.c_str());
    m.AddString("payload", ToHex(pkt.payload).c_str());
    m.AddInt32("hops", static_cast<int32>(pkt.path.size()));
    if (pkt.has_advert) {
        m.AddString("advert_name", pkt.advert.adv_name.c_str());
        m.AddString("advert_key", pkt.advert.public_key.c_str());
    }
    fTarget.SendMessage(&m);
}
