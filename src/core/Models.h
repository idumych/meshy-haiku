// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Application data model, ported from upstream Meshy src/models.py.
//
// Timestamps are doubles holding seconds since the Unix epoch, which is what
// SQLite stores in the REAL columns and what Python's datetime.timestamp()
// produces.  A negative value means "unset", mirroring Python's None.

#ifndef MESHY_CORE_MODELS_H
#define MESHY_CORE_MODELS_H

#include <stdint.h>

#include <string>
#include <vector>

#include "Bytes.h"

namespace meshy {

static const double kNoTime = -1.0;
inline bool HasTime(double t) { return t >= 0.0; }

enum ContactTypeEnum {
    CONTACT_CHAT = 1,
    CONTACT_REPEATER = 2,
    CONTACT_ROOM = 3,
    CONTACT_SENSOR = 4,
};

enum MessageStatusEnum {
    MSG_PENDING = 1,
    MSG_SENT = 2,
    MSG_DELIVERED = 3,
    MSG_FAILED = 4,
};

enum ContactFlags {
    CONTACT_FLAG_FAVORITE = 0x01,
    CONTACT_FLAG_TELE_BASE = 0x02,
    CONTACT_FLAG_TELE_LOC = 0x04,
    CONTACT_FLAG_TELE_ENV = 0x08,
    CONTACT_FLAG_REMOTE_CLI = 0x10,  // allow remote CLI, fw >= 1.18
};

enum NotificationLevel {
    NOTIFY_DEFAULT = -1,
    NOTIFY_ALL = 0,
    NOTIFY_MENTIONS = 1,
    NOTIFY_NONE = 2,
};

// Tri-state: -1 follows the global default, 0 off, 1 on.
enum SmartRoute {
    SMART_ROUTE_DEFAULT = -1,
    SMART_ROUTE_OFF = 0,
    SMART_ROUTE_ON = 1,
};

struct Contact {
    ByteBuffer public_key;
    std::string name;
    std::string custom_name;  // user alias; falls back to name when empty
    int type = CONTACT_CHAT;
    int flags = 0;
    int path_length = -1;  // -1 = flood
    ByteBuffer path;
    int path_hash_size = 1;  // 1, 2 or 3 bytes per hop
    bool force_flood = false;
    bool auto_reset_path = true;
    int smart_route_selection = SMART_ROUTE_DEFAULT;
    bool has_location = false;
    double latitude = 0.0;
    double longitude = 0.0;
    double last_seen = kNoTime;
    double received_at = kNoTime;
    double device_lastmod = kNoTime;
    double last_message_at = kNoTime;
    bool is_active = true;

    std::string PublicKeyHex() const { return ToHex(public_key); }
    // Short prefix used for display and for matching 6-byte wire prefixes.
    std::string KeyPrefix(size_t bytes = 6) const;
    const std::string& DisplayName() const { return custom_name.empty() ? name : custom_name; }
    bool IsFavorite() const { return (flags & CONTACT_FLAG_FAVORITE) != 0; }
    bool SupportsChat() const { return type == CONTACT_CHAT || type == CONTACT_ROOM; }
};

struct Message {
    int64_t id = 0;  // database row id, 0 when not yet stored
    ByteBuffer sender_key;
    std::string contact_key_hex;
    std::string text;
    double timestamp = kNoTime;
    bool is_outgoing = false;
    bool is_cli = false;
    int status = MSG_PENDING;
    std::string message_id;
    int retry_count = 0;
    bool has_expected_ack = false;
    uint32_t expected_ack_hash = 0;
    double sent_at = kNoTime;
    double delivered_at = kNoTime;
    bool has_trip_time = false;
    int trip_time_ms = 0;
    int path_length = -1;
    ByteBuffer path_bytes;
    std::string room_sender_name;  // resolved sender name for room messages
    std::string uid;               // content hash (incoming) or UUID (outgoing)
    bool read = false;             // outgoing messages are always read
};

struct ChannelMessage {
    int64_t id = 0;
    int channel_index = 0;
    std::string sender_name;
    std::string text;
    double timestamp = kNoTime;
    bool is_outgoing = false;
    bool read = false;
    std::string uid;
    // Path metadata, joined from channel_message_paths when available.
    bool has_snr = false;
    double snr = 0.0;
    int path_len = -1;
    std::string path_bytes;  // hex
    std::string region;
};

struct Channel {
    int index = 0;
    std::string name;
    ByteBuffer psk;  // 16 bytes
    int notification_level = NOTIFY_DEFAULT;
    std::string region;
    int retention_days = 0;

    static const char* kPublicChannelPsk;  // "8b3387..." the default public key
    std::string DisplayName() const;
};

struct DeviceInfo {
    std::string name;
    std::string public_key;  // hex
    int tx_power = 0;
    int max_tx_power = 0;
    double radio_freq = 0.0;  // MHz
    double radio_bw = 0.0;    // kHz
    int radio_sf = 0;
    int radio_cr = 0;
    bool cr_old_encoding = false;
    int fw_ver = 0;
    std::string fw_build;
    std::string model;
    std::string ver;
    int max_contacts = 0;
    int max_channels = 0;
    uint32_t ble_pin = 0;
    bool repeat = false;
    int path_hash_mode = 0;
    bool has_location = false;
    double adv_lat = 0.0;
    double adv_lon = 0.0;
    int battery_mv = 0;
    uint32_t storage_used_kb = 0;
    uint32_t storage_total_kb = 0;
};

struct PathHistoryEntry {
    int64_t id = 0;
    std::string contact_key_hex;
    std::string path_bytes;  // hex
    int path_hash_size = 1;
    int hop_count = 0;
    int success_count = 0;
    int failure_count = 0;
    bool has_trip_ms = false;
    int last_trip_ms = 0;
    double route_weight = 2.5;
    double last_success_at = kNoTime;
};

struct DiscoveredContact {
    std::string public_key_hex;
    std::string name;
    int type = CONTACT_CHAT;
    int flags = 0;
    int path_length = -1;
    ByteBuffer path;
    int path_hash_size = 1;
    bool has_location = false;
    double latitude = 0.0;
    double longitude = 0.0;
    double last_seen = kNoTime;
    double received_at = kNoTime;
    std::string source = "advert";
};

// ─── Stable message identity ─────────────────────────────────────
//
// Incoming messages get a deterministic content hash, so the same logical
// message yields the same id on every instance and backups/sync converge
// instead of duplicating.  Outgoing messages get a fresh UUID.

// Timestamps are rounded to whole microseconds so a save/export/restore round
// trip cannot shift the hash.
int64_t CanonicalTimestamp(double t);

std::string ContentUidDm(const std::string& contact_key_hex, const std::string& sender_key_hex,
    bool is_outgoing, double timestamp, const std::string& text,
    const std::string& room_sender_name = std::string());
std::string ContentUidChannel(int channel_index, const std::string& sender_name, bool is_outgoing,
    double timestamp, const std::string& text);
std::string NewUid();

}  // namespace meshy

#endif  // MESHY_CORE_MODELS_H
