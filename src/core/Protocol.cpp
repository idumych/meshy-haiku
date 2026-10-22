// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Protocol.h"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "Crypto.h"
#include "Smaz.h"

namespace meshy {

// Message overhead: cmd(1) + type(1) + attempt(1) + ts(4) + pubkey(6) + null(1) + safety(2)
static const size_t kDmOverhead = 16;
// Channel overhead: cmd(1) + type(1) + chan_idx(1) + ts(4) + null(1) + safety(2)
static const size_t kChanOverhead = 10;

static uint32_t _NowTs()
{
    return static_cast<uint32_t>(time(NULL));
}

static uint32_t _TsOrNow(uint32_t ts)
{
    return ts != 0 ? ts : _NowTs();
}

static int32_t _LatLonToFixed(double v)
{
    // Python's round() is banker's rounding, but the values here are never
    // exact .5 at micro-degree scale; lround matches in every practical case.
    return static_cast<int32_t>(lround(v * 1e6));
}

size_t MaxDmTextBytes()
{
    size_t a = MAX_FRAME_SIZE - kDmOverhead;
    return a < MAX_TEXT_PAYLOAD ? a : MAX_TEXT_PAYLOAD;
}

size_t MaxChannelTextBytes(const std::string& sender_name)
{
    size_t prefix_len = sender_name.size() + 2;  // ": " appended
    size_t a = prefix_len >= MAX_TEXT_PAYLOAD ? 0 : MAX_TEXT_PAYLOAD - prefix_len;
    size_t b = MAX_FRAME_SIZE - kChanOverhead;
    return a < b ? a : b;
}

const char* ErrorCodeMessage(uint8_t code)
{
    switch (code) {
        case 0x01: return "Unsupported command";
        case 0x02: return "Not found";
        case 0x03: return "Table full";
        case 0x04: return "Bad state";
        case 0x05: return "File I/O error";
        case 0x06: return "Illegal argument";
        default: return NULL;
    }
}

// ─── Path-length helpers ─────────────────────────────────────────

void DecodePathLenByte(uint8_t plen, int* hop_count, int* hash_size)
{
    *hash_size = ((plen >> 6) & 0x03) + 1;
    *hop_count = plen & 0x3F;
}

void ParsePathLengthByte(uint8_t plen, int* hop_count, int* hash_size)
{
    if (plen == 255) {
        *hop_count = -1;
        *hash_size = 1;
        return;
    }
    DecodePathLenByte(plen, hop_count, hash_size);
    // hash_size 4 is reserved (firmware 1.17+ rejects it)
    if (*hash_size == 4 || (*hop_count) * (*hash_size) > 64) {
        *hop_count = -1;
        *hash_size = 1;
    }
}

uint8_t EncodePathLen(int hop_count, int hash_size)
{
    if (hop_count < 0)
        return 0xFF;
    int hash_mode = hash_size - 1;
    if (hash_mode < 0)
        hash_mode = 0;
    hash_mode &= 0x03;
    return static_cast<uint8_t>((hop_count & 0x3F) | (hash_mode << 6));
}

double ParseSnr(int8_t raw)
{
    return static_cast<double>(raw) / 4.0;
}

double ParseSnr16(int16_t raw)
{
    return static_cast<double>(raw) / 4.0;
}

ByteBuffer ComputeScopeKey(const std::string& scope)
{
    std::string s = scope;
    if (s.empty() || s == "0" || s == "None" || s == "*")
        return ByteBuffer(16, 0);
    if (s[0] != '#')
        s = "#" + s;
    ByteBuffer h = crypto::Sha256(s);
    h.resize(16);
    return h;
}

ByteBuffer HashtagChannelPsk(const std::string& name)
{
    std::string lower = name;
    for (size_t i = 0; i < lower.size(); i++) {
        if (lower[i] >= 'A' && lower[i] <= 'Z')
            lower[i] = static_cast<char>(lower[i] - 'A' + 'a');
    }
    ByteBuffer h = crypto::Sha256(lower);
    h.resize(16);
    return h;
}

// ─── Frame builders ──────────────────────────────────────────────

ByteBuffer BuildAppStart(const std::string& app_name, uint8_t app_version)
{
    ByteWriter w;
    w.U8(CMD_APP_START);
    w.U8(app_version);
    w.Zeros(6);
    w.StrNul(app_name);
    return w.Take();
}

ByteBuffer BuildDeviceQuery(uint8_t app_version)
{
    ByteWriter w;
    w.U8(CMD_DEVICE_QUERY);
    w.U8(app_version);
    return w.Take();
}

ByteBuffer BuildGetContacts()
{
    ByteWriter w;
    w.U8(CMD_GET_CONTACTS);
    return w.Take();
}

ByteBuffer BuildGetContactsSince(uint32_t since)
{
    ByteWriter w;
    w.U8(CMD_GET_CONTACTS);
    w.U32(since);
    return w.Take();
}

ByteBuffer BuildGetDeviceTime()
{
    ByteWriter w;
    w.U8(CMD_GET_DEVICE_TIME);
    return w.Take();
}

ByteBuffer BuildSetDeviceTime(uint32_t timestamp)
{
    ByteWriter w;
    w.U8(CMD_SET_DEVICE_TIME);
    w.U32(_TsOrNow(timestamp));
    return w.Take();
}

ByteBuffer BuildGetBattAndStorage()
{
    ByteWriter w;
    w.U8(CMD_GET_BATT_AND_STORAGE);
    return w.Take();
}

ByteBuffer BuildSyncNextMessage()
{
    ByteWriter w;
    w.U8(CMD_SYNC_NEXT_MESSAGE);
    return w.Take();
}

ByteBuffer BuildSendTextMsg(const ByteBuffer& recipient_pub_key, const std::string& text,
    uint8_t attempt, uint32_t timestamp)
{
    ByteWriter w;
    w.U8(CMD_SEND_TXT_MSG);
    w.U8(TXT_TYPE_PLAIN);
    w.U8(attempt);
    w.U32(_TsOrNow(timestamp));
    w.Bytes(recipient_pub_key.data(), recipient_pub_key.size() < 6 ? recipient_pub_key.size() : 6);
    w.StrNul(text);
    return w.Take();
}

ByteBuffer BuildSendChannelTextMsg(uint8_t channel_index, const std::string& text,
    uint32_t timestamp)
{
    ByteWriter w;
    w.U8(CMD_SEND_CHANNEL_TXT_MSG);
    w.U8(TXT_TYPE_PLAIN);
    w.U8(channel_index);
    w.U32(_TsOrNow(timestamp));
    w.StrNul(text);
    return w.Take();
}

uint8_t CliTxtType(int contact_type, bool supports_cli)
{
    if (contact_type == ADV_TYPE_CHAT && supports_cli)
        return TXT_TYPE_CLI_COMMAND;
    return TXT_TYPE_CLI_DATA;
}

ByteBuffer BuildSendCliCommand(const ByteBuffer& pub_key, const std::string& command,
    uint8_t attempt, uint32_t timestamp, uint8_t txt_type)
{
    ByteWriter w;
    w.U8(CMD_SEND_TXT_MSG);
    w.U8(txt_type);
    w.U8(attempt);
    w.U32(_TsOrNow(timestamp));
    w.Bytes(pub_key.data(), pub_key.size() < 6 ? pub_key.size() : 6);
    w.StrNul(command);
    return w.Take();
}

ByteBuffer BuildRunCliCommand(const std::string& command)
{
    ByteWriter w;
    w.U8(CMD_RUN_CLI_COMMAND);
    w.Str(command);
    return w.Take();
}

ByteBuffer BuildSendSelfAdvert(bool flood)
{
    ByteWriter w;
    w.U8(CMD_SEND_SELF_ADVERT);
    if (flood)
        w.U8(1);
    return w.Take();
}

ByteBuffer BuildSetAdvertName(const std::string& name)
{
    ByteWriter w;
    w.U8(CMD_SET_ADVERT_NAME);
    w.StrTruncated(name, MAX_NAME_SIZE - 1);
    return w.Take();
}

ByteBuffer BuildSetAdvertLatLon(double lat, double lon)
{
    ByteWriter w;
    w.U8(CMD_SET_ADVERT_LATLON);
    w.I32(_LatLonToFixed(lat));
    w.I32(_LatLonToFixed(lon));
    w.I32(0);  // altitude (reserved)
    return w.Take();
}

ByteBuffer BuildRemoveContact(const ByteBuffer& pub_key)
{
    ByteWriter w;
    w.U8(CMD_REMOVE_CONTACT);
    w.Bytes(pub_key);
    return w.Take();
}

ByteBuffer BuildAddUpdateContact(const ByteBuffer& pub_key, uint8_t contact_type, uint8_t flags,
    uint8_t path_len, const ByteBuffer& path, const std::string& name, bool has_location,
    double lat, double lon, bool has_last_modified, uint32_t last_modified, uint32_t last_advert)
{
    ByteWriter w;
    w.U8(CMD_ADD_UPDATE_CONTACT);
    w.Bytes(pub_key);
    w.U8(contact_type);
    w.U8(flags);
    w.U8(path_len);
    w.BytesPadded(path, MAX_PATH_SIZE);
    w.StrTruncated(name, MAX_NAME_SIZE - 1, MAX_NAME_SIZE);
    w.U32(_TsOrNow(last_advert));
    if (has_location || has_last_modified) {
        w.I32(has_location ? _LatLonToFixed(lat) : 0);
        w.I32(has_location ? _LatLonToFixed(lon) : 0);
        if (has_last_modified)
            w.U32(last_modified);
    }
    return w.Take();
}

ByteBuffer BuildGetContactByKey(const ByteBuffer& pub_key)
{
    ByteWriter w;
    w.U8(CMD_GET_CONTACT_BY_KEY);
    w.Bytes(pub_key);
    return w.Take();
}

ByteBuffer BuildResetPath(const ByteBuffer& pub_key)
{
    ByteWriter w;
    w.U8(CMD_RESET_PATH);
    w.Bytes(pub_key);
    return w.Take();
}

ByteBuffer BuildShareContact(const ByteBuffer& pub_key)
{
    ByteWriter w;
    w.U8(CMD_SHARE_CONTACT);
    w.Bytes(pub_key);
    return w.Take();
}

ByteBuffer BuildExportContact(const ByteBuffer& pub_key)
{
    ByteWriter w;
    w.U8(CMD_EXPORT_CONTACT);
    w.Bytes(pub_key);
    return w.Take();
}

ByteBuffer BuildImportContact(const ByteBuffer& contact_frame)
{
    ByteWriter w;
    w.U8(CMD_IMPORT_CONTACT);
    w.Bytes(contact_frame);
    return w.Take();
}

ByteBuffer BuildGetChannel(uint8_t channel_index)
{
    ByteWriter w;
    w.U8(CMD_GET_CHANNEL);
    w.U8(channel_index);
    return w.Take();
}

ByteBuffer BuildSetChannel(uint8_t channel_index, const std::string& name, const ByteBuffer& psk)
{
    ByteWriter w;
    w.U8(CMD_SET_CHANNEL);
    w.U8(channel_index);
    w.StrTruncated(name, 32, 32);
    w.BytesPadded(psk, 16);
    return w.Take();
}

ByteBuffer BuildSetRadioParams(uint32_t freq_khz, uint32_t bw_khz, uint8_t sf, uint8_t cr)
{
    ByteWriter w;
    w.U8(CMD_SET_RADIO_PARAMS);
    w.U32(freq_khz);
    w.U32(bw_khz);
    w.U8(sf);
    w.U8(cr);
    return w.Take();
}

ByteBuffer BuildSetRadioParamsRepeat(uint32_t freq_khz, uint32_t bw_khz, uint8_t sf, uint8_t cr,
    bool client_repeat)
{
    ByteWriter w;
    w.U8(CMD_SET_RADIO_PARAMS);
    w.U32(freq_khz);
    w.U32(bw_khz);
    w.U8(sf);
    w.U8(cr);
    w.U8(client_repeat ? 1 : 0);
    return w.Take();
}

ByteBuffer BuildSetRadioTxPower(int power_dbm)
{
    ByteWriter w;
    w.U8(CMD_SET_RADIO_TX_POWER);
    w.U8(static_cast<uint8_t>(power_dbm & 0xFF));
    return w.Take();
}

ByteBuffer BuildReboot()
{
    ByteWriter w;
    w.U8(CMD_REBOOT);
    w.Str("reboot");
    return w.Take();
}

ByteBuffer BuildFactoryReset()
{
    ByteWriter w;
    w.U8(CMD_FACTORY_RESET);
    w.Str("reset");
    return w.Take();
}

ByteBuffer BuildSetPathHashMode(uint8_t mode)
{
    ByteWriter w;
    w.U8(CMD_SET_PATH_HASH_MODE);
    w.U8(0);
    w.U8(mode);
    return w.Take();
}

ByteBuffer BuildSetDevicePin(uint32_t pin)
{
    ByteWriter w;
    w.U8(CMD_SET_DEVICE_PIN);
    w.U32(pin);
    return w.Take();
}

ByteBuffer BuildSetOtherParams(uint8_t manual_add_contacts, uint8_t telemetry_flags,
    uint8_t advert_loc_policy, uint8_t multi_acks)
{
    ByteWriter w;
    w.U8(CMD_SET_OTHER_PARAMS);
    w.U8(manual_add_contacts);
    w.U8(telemetry_flags);
    w.U8(advert_loc_policy);
    w.U8(multi_acks);
    return w.Take();
}

ByteBuffer BuildGetCustomVars()
{
    ByteWriter w;
    w.U8(CMD_GET_CUSTOM_VARS);
    return w.Take();
}

ByteBuffer BuildSetCustomVar(const std::string& key_value)
{
    ByteWriter w;
    w.U8(CMD_SET_CUSTOM_VAR);
    w.StrNul(key_value);
    return w.Take();
}

ByteBuffer BuildSetFloodScope(const std::string& scope, bool unscoped)
{
    ByteWriter w;
    w.U8(CMD_SET_FLOOD_SCOPE);
    w.U8(unscoped ? 1 : 0);
    w.Bytes(ComputeScopeKey(scope));
    return w.Take();
}

ByteBuffer BuildSetDefaultFloodScope(const std::string& scope)
{
    ByteBuffer key = ComputeScopeKey(scope);
    bool all_zero = true;
    for (size_t i = 0; i < key.size(); i++) {
        if (key[i] != 0) {
            all_zero = false;
            break;
        }
    }
    ByteWriter w;
    w.U8(CMD_SET_DEFAULT_FLOOD_SCOPE);
    if (all_zero)
        return w.Take();  // clears the stored scope

    std::string name = scope;
    if (name.empty() || name[0] != '#')
        name = "#" + name;
    w.StrTruncated(name, 31, 31);
    w.Bytes(key);
    return w.Take();
}

ByteBuffer BuildGetDefaultFloodScope()
{
    ByteWriter w;
    w.U8(CMD_GET_DEFAULT_FLOOD_SCOPE);
    return w.Take();
}

ByteBuffer BuildGetAdvertPath(const ByteBuffer& pub_key)
{
    ByteWriter w;
    w.U8(CMD_GET_ADVERT_PATH);
    w.U8(0);
    w.Bytes(pub_key.data(), pub_key.size() < PUB_KEY_SIZE ? pub_key.size() : PUB_KEY_SIZE);
    return w.Take();
}

ByteBuffer BuildPathDiscovery(const ByteBuffer& pub_key)
{
    ByteWriter w;
    w.U8(CMD_PATH_DISCOVERY);
    w.U8(0);
    w.Bytes(pub_key);
    return w.Take();
}

ByteBuffer BuildTracePath(uint32_t tag, const ByteBuffer& path_bytes, uint8_t flags)
{
    ByteWriter w;
    w.U8(CMD_SEND_TRACE_PATH);
    w.U32(tag);
    w.U32(0);  // auth
    w.U8(flags);
    w.Bytes(path_bytes);
    return w.Take();
}

ByteBuffer BuildSendLogin(const ByteBuffer& recipient_pub_key, const std::string& password)
{
    ByteWriter w;
    w.U8(CMD_SEND_LOGIN);
    w.Bytes(recipient_pub_key);
    w.StrNul(password);
    return w.Take();
}

ByteBuffer BuildLogout(const ByteBuffer& pub_key)
{
    ByteWriter w;
    w.U8(CMD_LOGOUT);
    w.Bytes(pub_key);
    return w.Take();
}

ByteBuffer BuildSendStatusReq(const ByteBuffer& recipient_pub_key)
{
    ByteWriter w;
    w.U8(CMD_SEND_STATUS_REQ);
    w.Bytes(recipient_pub_key);
    return w.Take();
}

ByteBuffer BuildGetAutoAddConfig()
{
    ByteWriter w;
    w.U8(CMD_GET_AUTO_ADD_CONFIG);
    return w.Take();
}

ByteBuffer BuildSetAutoAddConfig(bool chat, bool repeater, bool room, bool sensor,
    bool overwrite_oldest, uint8_t max_hops)
{
    uint8_t flags = 0;
    if (chat) flags |= AUTO_ADD_CHAT;
    if (repeater) flags |= AUTO_ADD_REPEATER;
    if (room) flags |= AUTO_ADD_ROOM_SERVER;
    if (sensor) flags |= AUTO_ADD_SENSOR;
    if (overwrite_oldest) flags |= AUTO_ADD_OVERWRITE_OLDEST;

    ByteWriter w;
    w.U8(CMD_SET_AUTO_ADD_CONFIG);
    w.U8(flags);
    w.U8(max_hops > 64 ? 64 : max_hops);
    return w.Take();
}

ByteBuffer BuildSendDiscoverReq(uint8_t type_filter, bool prefix_only, uint32_t tag)
{
    if (tag == 0)
        tag = crypto::RandomU32();
    uint8_t flags = CTRL_NODE_DISCOVER_REQ;  // upper nibble is sub_type 8
    if (prefix_only)
        flags |= 0x01;

    ByteWriter w;
    w.U8(CMD_SEND_CONTROL_DATA);
    w.U8(flags);
    w.U8(type_filter);
    w.U32(tag);
    return w.Take();
}

ByteBuffer BuildGetNeighbors(const ByteBuffer& repeater_pub_key, uint8_t max_results,
    uint16_t offset, uint8_t key_len)
{
    ByteWriter w;
    w.U8(CMD_SEND_BINARY_REQ);
    w.Bytes(repeater_pub_key);
    w.U8(REQ_TYPE_GET_NEIGHBORS);
    w.U8(0x00);  // version/reserved
    w.U8(max_results);
    w.U16(offset);
    w.U8(0x00);  // order by
    w.U8(key_len);
    return w.Take();
}

ByteBuffer BuildGetAcl(const ByteBuffer& repeater_pub_key)
{
    ByteWriter w;
    w.U8(CMD_SEND_BINARY_REQ);
    w.Bytes(repeater_pub_key);
    w.U8(REQ_TYPE_ACL);
    w.U8(0x00);
    w.U8(0x00);
    return w.Take();
}

ByteBuffer BuildGetTelemetry(const ByteBuffer& repeater_pub_key)
{
    // [CMD][pubkey:32][req_type][reserved:4][random:4].  The responder reads
    // data[1] as an inverse permissions mask (0x00 → allow all); the random
    // tail keeps the packet hash unique.
    ByteWriter w;
    w.U8(CMD_SEND_BINARY_REQ);
    w.Bytes(repeater_pub_key);
    w.U8(REQ_TYPE_TELEMETRY);
    w.Zeros(4);
    w.Bytes(crypto::RandomBytes(4));
    return w.Take();
}

ByteBuffer BuildGetTelemetryChat(const ByteBuffer& pub_key)
{
    ByteWriter w;
    w.U8(CMD_SEND_TELEMETRY_REQ);
    w.Zeros(3);  // reserved
    if (pub_key.size() >= 32)
        w.Bytes(pub_key.data(), 32);
    return w.Take();
}

static ByteBuffer _BuildAnonReq(const ByteBuffer& pub_key, uint8_t req_type, int path_length,
    const ByteBuffer& path, int path_hash_size)
{
    // [CMD][pub_key_32][req_type][reply_path_header][path reversed by hop]
    int hash_mode = path_hash_size - 1;
    if (hash_mode < 0)
        hash_mode = 0;
    int hop_count = path_length < 0 ? 0 : (path_length > 63 ? 63 : path_length);
    size_t stride = path_hash_size > 0 ? static_cast<size_t>(path_hash_size) : 1;

    ByteWriter w;
    w.U8(CMD_SEND_ANON_REQ);
    w.Bytes(pub_key.data(), pub_key.size() < 32 ? pub_key.size() : 32);
    w.U8(req_type);
    // The header reports the requested hop count even when the supplied path
    // holds fewer hops; upstream slices the short path and emits only the hops
    // that exist, so the two must stay in step.
    w.U8(static_cast<uint8_t>(hop_count | (hash_mode << 6)));
    for (int i = hop_count - 1; i >= 0; i--) {
        size_t start = static_cast<size_t>(i) * stride;
        if (start >= path.size())
            continue;
        size_t n = path.size() - start;
        if (n > stride)
            n = stride;
        w.Bytes(path.data() + start, n);
    }
    return w.Take();
}

ByteBuffer BuildAnonReqRegions(const ByteBuffer& pub_key, int path_length, const ByteBuffer& path,
    int path_hash_size)
{
    return _BuildAnonReq(pub_key, ANON_REQ_REGIONS, path_length, path, path_hash_size);
}

ByteBuffer BuildAnonReqOwner(const ByteBuffer& pub_key, int path_length, const ByteBuffer& path,
    int path_hash_size)
{
    return _BuildAnonReq(pub_key, ANON_REQ_OWNER, path_length, path, path_hash_size);
}

ByteBuffer BuildGetAllowedRepeatFreq()
{
    ByteWriter w;
    w.U8(CMD_GET_ALLOWED_REPEAT_FREQ);
    return w.Take();
}

ByteBuffer BuildGetStats(uint8_t sub_type)
{
    ByteWriter w;
    w.U8(CMD_GET_STATS);
    w.U8(sub_type);
    return w.Take();
}

ByteBuffer BuildExportPrivateKey()
{
    ByteWriter w;
    w.U8(CMD_EXPORT_PRIVATE_KEY);
    return w.Take();
}

ByteBuffer BuildImportPrivateKey(const ByteBuffer& key)
{
    ByteWriter w;
    w.U8(CMD_IMPORT_PRIVATE_KEY);
    w.Bytes(key.data(), key.size() < 64 ? key.size() : 64);
    return w.Take();
}

// ─── Frame parsers ───────────────────────────────────────────────

int IdentifyResponse(const ByteBuffer& data)
{
    if (data.empty())
        return -1;
    return data[0];
}

ContactFrame ParseContactFrame(const ByteBuffer& data)
{
    ContactFrame c;
    if (data.size() < 100)
        return c;
    uint8_t code = data[0];
    if (code != RESP_CODE_CONTACT && code != PUSH_CODE_NEW_ADVERT)
        return c;

    ByteReader r(data.data() + 1, data.size() - 1);
    c.public_key = ToHex(r.Read(32));
    c.type = r.U8();
    c.flags = r.U8();
    uint8_t plen = r.U8();
    ParsePathLengthByte(plen, &c.out_path_len, &c.path_hash_size);
    ByteBuffer raw_path = r.Read(64);

    int hop_count = c.out_path_len > 0 ? c.out_path_len : 0;
    size_t path_byte_len = static_cast<size_t>(hop_count) * static_cast<size_t>(c.path_hash_size);
    if (path_byte_len > raw_path.size())
        path_byte_len = raw_path.size();
    if (path_byte_len > 0)
        c.out_path = ToHex(raw_path.data(), path_byte_len);

    c.adv_name = r.ReadFixedStringStripNuls(32);
    c.last_advert = r.U32();
    c.adv_lat = static_cast<double>(r.I32()) / 1e6;
    c.adv_lon = static_cast<double>(r.I32()) / 1e6;
    if (r.Has(4)) {
        c.lastmod = r.U32();
        c.has_lastmod = true;
    }
    c.valid = true;
    return c;
}

SelfInfo ParseSelfInfo(const ByteBuffer& data)
{
    SelfInfo si;
    // Fixed fields after the code byte total 57 bytes; anything shorter would
    // read past the end below.
    if (data.size() < 58 || data[0] != RESP_CODE_SELF_INFO)
        return si;

    ByteReader r(data.data() + 1, data.size() - 1);
    si.adv_type = r.U8();
    si.tx_power = r.I8();
    si.max_tx_power = r.U8();
    si.public_key = ToHex(r.Read(32));
    si.adv_lat = static_cast<double>(r.I32()) / 1e6;
    si.adv_lon = static_cast<double>(r.I32()) / 1e6;
    si.multi_acks = r.U8();
    si.adv_loc_policy = r.U8();
    uint8_t telemetry_mode = r.U8();
    si.telemetry_mode_env = (telemetry_mode >> 4) & 0x03;
    si.telemetry_mode_loc = (telemetry_mode >> 2) & 0x03;
    si.telemetry_mode_base = telemetry_mode & 0x03;
    si.manual_add_contacts = r.U8() > 0;
    si.radio_freq = static_cast<double>(r.U32()) / 1000.0;
    si.radio_bw = static_cast<double>(r.U32()) / 1000.0;
    si.radio_sf = r.U8();
    si.radio_cr = r.U8();
    si.name = r.ReadRemainingString();
    si.valid = r.Ok();
    return si;
}

DeviceInfoFrame ParseDeviceInfo(const ByteBuffer& data)
{
    DeviceInfoFrame d;
    if (data.size() < 4 || data[0] != RESP_CODE_DEVICE_INFO)
        return d;

    ByteReader r(data.data() + 1, data.size() - 1);
    d.fw_ver = r.U8();
    if (d.fw_ver >= 3) {
        d.max_contacts = static_cast<uint16_t>(r.U8()) * 2;
        d.max_channels = r.U8();
        d.ble_pin = r.U32();
        d.fw_build = r.ReadFixedStringStripNuls(12);
        d.model = r.ReadFixedStringStripNuls(40);
        d.ver = r.ReadFixedStringStripNuls(20);
    }
    if (d.fw_ver >= 9 && r.Has(1)) {
        d.repeat = r.U8() != 0;
        d.has_repeat = true;
    }
    if (d.fw_ver >= 10 && r.Has(1)) {
        d.path_hash_mode = r.U8();
        d.has_path_hash_mode = true;
    }
    d.valid = true;
    return d;
}

BattAndStorage ParseBattAndStorage(const ByteBuffer& data)
{
    BattAndStorage b;
    if (data.size() < 3 || data[0] != RESP_CODE_BATT_AND_STORAGE)
        return b;
    ByteReader r(data.data() + 1, data.size() - 1);
    b.level = r.U16();
    if (data.size() > 3) {
        b.used_kb = r.U32();
        b.total_kb = r.U32();
        b.has_storage = r.Ok();
    }
    b.valid = true;
    return b;
}

ContactMessage ParseContactMessage(const ByteBuffer& data)
{
    ContactMessage m;
    if (data.empty())
        return m;
    uint8_t code = data[0];
    size_t min_len;
    // V1 carries pubkey_prefix(6) + plen(1) + txt_type(1) + timestamp(4) after
    // the code; the V3 variant prepends SNR(1) + reserved(2).
    if (code == RESP_CODE_CONTACT_MSG_RECV)
        min_len = 13;
    else if (code == RESP_CODE_CONTACT_MSG_RECV_V3)
        min_len = 16;
    else
        return m;
    if (data.size() < min_len)
        return m;

    ByteReader r(data.data() + 1, data.size() - 1);
    if (code == RESP_CODE_CONTACT_MSG_RECV_V3) {
        m.snr = ParseSnr(r.I8());
        m.has_snr = true;
        r.Skip(2);  // reserved
    }
    m.pubkey_prefix = ToHex(r.Read(6));
    uint8_t plen = r.U8();
    ParsePathLengthByte(plen, &m.path_len, &m.path_hash_size);
    m.txt_type = r.U8();
    m.sender_timestamp = r.U32();
    if (m.txt_type == TXT_TYPE_SIGNED)
        m.signature = ToHex(r.Read(4));
    m.text = smaz::TryDecodePrefixed(r.ReadRemainingString());
    m.valid = true;
    return m;
}

ChannelMessageFrame ParseChannelMessage(const ByteBuffer& data)
{
    ChannelMessageFrame m;
    if (data.empty())
        return m;
    uint8_t code = data[0];
    size_t min_len;
    if (code == RESP_CODE_CHANNEL_MSG_RECV)
        min_len = 8;
    else if (code == RESP_CODE_CHANNEL_MSG_RECV_V3)
        min_len = 11;
    else
        return m;
    if (data.size() < min_len)
        return m;

    ByteReader r(data.data() + 1, data.size() - 1);
    if (code == RESP_CODE_CHANNEL_MSG_RECV_V3) {
        m.snr = ParseSnr(r.I8());
        m.has_snr = true;
        r.Skip(2);  // reserved
    }
    m.channel_idx = r.U8();
    uint8_t plen = r.U8();
    ParsePathLengthByte(plen, &m.path_len, &m.path_hash_size);
    m.txt_type = r.U8();
    m.sender_timestamp = r.U32();
    m.text = r.ReadRemainingStringStripBoth();

    // Channel messages carry "name: message".
    size_t sep = m.text.find(": ");
    if (sep != std::string::npos) {
        m.sender_name = m.text.substr(0, sep);
        m.msg_text = m.text.substr(sep + 2);
    } else {
        m.sender_name.clear();
        m.msg_text = m.text;
    }
    m.msg_text = smaz::TryDecodePrefixed(m.msg_text);
    m.valid = true;
    return m;
}

ChannelData ParseChannelData(const ByteBuffer& data)
{
    ChannelData d;
    if (data.size() < 9 || data[0] != RESP_CODE_CHANNEL_DATA_RECV)
        return d;
    ByteReader r(data.data() + 1, data.size() - 1);
    d.snr = ParseSnr(r.I8());
    r.Skip(2);  // reserved
    d.channel_idx = r.U8();
    uint8_t plen = r.U8();
    ParsePathLengthByte(plen, &d.path_len, &d.path_hash_size);
    d.data_type = r.U16();
    d.data_len = r.U8();
    d.payload = ToHex(r.Read(d.data_len));
    d.valid = true;
    return d;
}

ChannelInfo ParseChannelInfo(const ByteBuffer& data)
{
    ChannelInfo c;
    if (data.size() < 50 || data[0] != RESP_CODE_CHANNEL_INFO)
        return c;
    ByteReader r(data.data() + 1, data.size() - 1);
    c.channel_idx = r.U8();
    c.channel_name = r.ReadFixedStringAtNul(32);
    c.channel_secret = r.Read(16);
    c.valid = true;
    return c;
}

SentResponse ParseSentResponse(const ByteBuffer& data)
{
    SentResponse s;
    if (data.size() < 2 || data[0] != RESP_CODE_SENT)
        return s;
    ByteReader r(data.data() + 1, data.size() - 1);
    s.type = r.U8();
    s.expected_ack = r.Read(4);
    s.suggested_timeout = r.U32();
    s.valid = true;
    return s;
}

SendConfirmed ParsePushSendConfirmed(const ByteBuffer& data)
{
    SendConfirmed s;
    if (data.size() < 5 || data[0] != PUSH_CODE_SEND_CONFIRMED)
        return s;
    s.code = ToHex(data.data() + 1, 4);
    if (data.size() >= 9) {
        ByteReader r(data.data() + 5, data.size() - 5);
        s.trip_time_ms = r.U32();
        s.has_trip_time = true;
    }
    s.valid = true;
    return s;
}

AutoAddConfig ParseAutoAddConfig(const ByteBuffer& data)
{
    AutoAddConfig a;
    if (data.size() < 2 || data[0] != RESP_CODE_AUTO_ADD_CONFIG)
        return a;
    a.config = data[1];
    a.max_hops = data.size() >= 3 ? data[2] : 0;
    a.valid = true;
    return a;
}

DefaultFloodScope ParseDefaultFloodScope(const ByteBuffer& data)
{
    DefaultFloodScope s;
    if (data.empty() || data[0] != RESP_CODE_DEFAULT_FLOOD_SCOPE)
        return s;
    s.valid = true;
    // 48 bytes = scope set; a 1-byte frame means none configured.
    if (data.size() >= 48) {
        ByteReader r(data.data() + 1, data.size() - 1);
        s.scope_name = r.ReadFixedStringStripNuls(31);
        s.scope_key = ToHex(r.Read(16));
        s.has_scope = true;
    }
    return s;
}

ErrorResponse ParseErrorResponse(const ByteBuffer& data)
{
    ErrorResponse e;
    e.valid = true;
    if (data.size() < 2) {
        e.code = 0;
        e.message = "Unknown error";
        return e;
    }
    e.code = data[1];
    const char* msg = ErrorCodeMessage(e.code);
    if (msg != NULL) {
        e.message = msg;
    } else {
        char buf[48];
        snprintf(buf, sizeof(buf), "Unknown error (0x%02x)", e.code);
        e.message = buf;
    }
    return e;
}

AdvertPathResponse ParseAdvertPathResponse(const ByteBuffer& data)
{
    AdvertPathResponse p;
    if (data.size() < 6 || data[0] != RESP_CODE_ADVERT_PATH)
        return p;
    ByteReader r(data.data() + 1, data.size() - 1);
    p.timestamp = r.U32();
    uint8_t plen = r.U8();
    p.valid = true;
    if (plen == 255) {
        p.path_length = -1;
        p.path_hash_size = 1;
        return p;
    }
    DecodePathLenByte(plen, &p.path_length, &p.path_hash_size);
    size_t path_byte_len
        = static_cast<size_t>(p.path_length) * static_cast<size_t>(p.path_hash_size);
    if (path_byte_len > r.Remaining())
        path_byte_len = r.Remaining();
    p.path = r.Read(path_byte_len);
    for (size_t i = 0; i + p.path_hash_size <= p.path.size(); i += p.path_hash_size)
        p.hops.push_back(ToHexUpper(p.path.data() + i, p.path_hash_size));
    return p;
}

TraceResponse ParseTraceResponse(const ByteBuffer& data)
{
    TraceResponse t;
    if (data.size() < 12 || data[0] != PUSH_CODE_TRACE_DATA)
        return t;
    size_t path_byte_len = data[2];
    uint8_t flags = data[3];
    t.path_hash_size = 1 << (flags & 3);
    size_t hop_count = t.path_hash_size > 0 ? path_byte_len / t.path_hash_size : path_byte_len;
    t.tag.assign(data.begin() + 4, data.begin() + 8);

    const size_t offset = 12;  // path data starts here
    if (offset + path_byte_len > data.size())
        return t;
    for (size_t i = 0; i < hop_count; i++) {
        size_t start = offset + i * t.path_hash_size;
        t.path_hops.push_back(
            ByteBuffer(data.begin() + start, data.begin() + start + t.path_hash_size));
    }
    for (size_t i = offset + path_byte_len; i < data.size(); i++)
        t.snr_values.push_back(ParseSnr(static_cast<int8_t>(data[i])));
    t.valid = true;
    return t;
}

StatusResponse ParseStatusResponse(const ByteBuffer& data)
{
    StatusResponse s;
    if (data.size() < 60)  // 8 header + 52 payload
        return s;
    // Skip code(1) + reserved(1) + sender_prefix(6).
    ByteReader r(data.data() + 8, data.size() - 8);
    size_t avail = data.size() - 8;
    s.battery_mv = r.U16();
    s.queue_len = r.U16();
    s.noise_floor = r.I16();
    s.last_rssi = r.I16();
    s.packets_recv = r.U32();
    s.packets_sent = r.U32();
    s.tx_air_secs = r.U32();
    s.uptime_secs = r.U32();
    s.flood_tx = r.U32();
    s.direct_tx = r.U32();
    s.flood_rx = r.U32();
    s.direct_rx = r.U32();
    s.error_events = r.U16();
    s.last_snr = ParseSnr16(r.I16());  // 16-bit field here, unlike other frames
    s.dup_direct = avail >= 46 ? r.U16() : 0;
    s.dup_flood = avail >= 48 ? r.U16() : 0;
    s.rx_air_secs = avail >= 52 ? r.U32() : 0;
    if (avail >= 56) {
        s.recv_errors = r.U32();
        s.has_recv_errors = true;
    }
    s.valid = true;
    return s;
}

NeighborsResponse ParseNeighborsResponse(const ByteBuffer& data, uint8_t key_len)
{
    NeighborsResponse n;
    if (data.size() < 10)
        return n;
    // Skip code(1) + reserved(1) + tag(4).
    ByteReader r(data.data() + 6, data.size() - 6);
    n.total = r.U16();
    uint16_t results_count = r.U16();
    size_t entry_size = static_cast<size_t>(key_len) + 4 + 1;
    for (uint16_t i = 0; i < results_count; i++) {
        if (r.Remaining() < entry_size)
            break;
        NeighborEntry e;
        e.pub_key_hex = ToHex(r.Read(key_len));
        e.last_heard = r.U32();
        e.snr = ParseSnr(r.I8());
        n.neighbors.push_back(e);
    }
    return n;
}

DiscoverResponse ParseDiscoverResponse(const ByteBuffer& data)
{
    DiscoverResponse d;
    if (data.size() < 8)
        return d;
    // data[0] = 0x8E, data[1:3] = prefix bytes, data[3] = path length byte.
    size_t offset = 3;
    int hop_count, hash_size;
    DecodePathLenByte(data[offset], &hop_count, &hash_size);
    offset += 1;
    offset += static_cast<size_t>(hop_count) * static_cast<size_t>(hash_size);

    if (offset + 6 > data.size())  // need flags + snr + tag(4)
        return d;

    uint8_t flags = data[offset];
    uint8_t sub_type = (flags >> 4) & 0x0F;
    if (sub_type != 0x09)  // DISCOVER_RESP
        return d;

    d.node_type = flags & 0x0F;
    d.snr = ParseSnr(static_cast<int8_t>(data[offset + 1]));
    ByteReader r(data.data() + offset + 2, data.size() - offset - 2);
    d.tag = r.U32();
    d.pub_key_hex = ToHex(r.ReadRemaining());
    d.valid = true;
    return d;
}

StatsResponse ParseStatsResponse(const ByteBuffer& data)
{
    StatsResponse s;
    if (data.size() < 4)
        return s;
    s.sub_type = data[1];
    s.valid = true;

    if (s.sub_type == STATS_TYPE_CORE && data.size() >= 11) {
        ByteReader r(data.data() + 2, data.size() - 2);
        s.battery_mv = r.U16();
        s.uptime_secs = r.U32();
        s.errors = r.U16();
        s.queue_len = r.U8();
    } else if (s.sub_type == STATS_TYPE_RADIO && data.size() >= 14) {
        ByteReader r(data.data() + 2, data.size() - 2);
        s.noise_floor = r.I16();
        s.last_rssi = r.I8();
        s.last_snr = ParseSnr(r.I8());
        s.tx_air_secs = r.U32();
        s.rx_air_secs = r.U32();
    } else if (s.sub_type == STATS_TYPE_PACKETS && data.size() >= 26) {
        ByteReader r(data.data() + 2, data.size() - 2);
        s.recv = r.U32();
        s.sent = r.U32();
        s.flood_tx = r.U32();
        s.direct_tx = r.U32();
        s.flood_rx = r.U32();
        s.direct_rx = r.U32();
        if (data.size() >= 30) {
            s.recv_errors = r.U32();
            s.has_recv_errors = true;
        }
    }
    return s;
}

std::vector<FreqRange> ParseAllowedRepeatFreq(const ByteBuffer& data)
{
    std::vector<FreqRange> ranges;
    size_t offset = 1;
    while (offset + 8 <= data.size()) {
        ByteReader r(data.data() + offset, 8);
        FreqRange fr;
        fr.lower_khz = r.U32();
        fr.upper_khz = r.U32();
        if (fr.lower_khz == 0 && fr.upper_khz == 0)
            break;
        ranges.push_back(fr);
        offset += 8;
    }
    return ranges;
}

std::vector<std::pair<std::string, std::string> > ParseCustomVars(const ByteBuffer& data)
{
    std::vector<std::pair<std::string, std::string> > out;
    if (data.size() < 2 || data[0] != RESP_CODE_CUSTOM_VARS)
        return out;
    ByteReader r(data.data() + 1, data.size() - 1);
    std::string raw = r.ReadRemainingString();
    if (raw.empty())
        return out;

    size_t start = 0;
    while (start <= raw.size()) {
        size_t comma = raw.find(',', start);
        std::string pair
            = raw.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        size_t colon = pair.find(':');
        if (colon != std::string::npos)
            out.push_back(std::make_pair(pair.substr(0, colon), pair.substr(colon + 1)));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return out;
}

bool ParseCurrTime(const ByteBuffer& data, uint32_t* out_time)
{
    if (data.size() < 5 || data[0] != RESP_CODE_CURR_TIME)
        return false;
    ByteReader r(data.data() + 1, data.size() - 1);
    *out_time = r.U32();
    return true;
}

bool ParseCliReply(const ByteBuffer& data, std::string* out_text)
{
    if (data.empty() || data[0] != RESP_CODE_CLI_REPLY)
        return false;
    ByteReader r(data.data() + 1, data.size() - 1);
    *out_text = r.ReadRemainingString();
    return true;
}

// ─── Cayenne LPP ─────────────────────────────────────────────────

const char* LppTypeName(uint8_t type)
{
    switch (type) {
        case LPP_DIGITAL_INPUT: return "Digital Input";
        case LPP_DIGITAL_OUTPUT: return "Digital Output";
        case LPP_ANALOG_INPUT: return "Analog Input";
        case LPP_ANALOG_OUTPUT: return "Analog Output";
        case LPP_GENERIC_SENSOR: return "Generic Sensor";
        case LPP_LUMINOSITY: return "Luminosity";
        case LPP_PRESENCE: return "Presence";
        case LPP_TEMPERATURE: return "Temperature";
        case LPP_HUMIDITY: return "Humidity";
        case LPP_ACCELEROMETER: return "Accelerometer";
        case LPP_PRESSURE: return "Pressure";
        case LPP_VOLTAGE: return "Voltage";
        case LPP_CURRENT: return "Current";
        case LPP_FREQUENCY: return "Frequency";
        case LPP_PERCENTAGE: return "Percentage";
        case LPP_ALTITUDE: return "Altitude";
        case LPP_CONCENTRATION: return "Concentration";
        case LPP_POWER: return "Power";
        case LPP_DISTANCE: return "Distance";
        case LPP_ENERGY: return "Energy";
        case LPP_DIRECTION: return "Direction";
        case LPP_UNIX_TIME: return "Time";
        case LPP_GYROMETER: return "Gyrometer";
        case LPP_COLOUR: return "Colour";
        case LPP_GPS: return "GPS";
        case LPP_SWITCH: return "Switch";
        default: return "Unknown";
    }
}

const char* LppTypeUnit(uint8_t type)
{
    switch (type) {
        case LPP_ANALOG_INPUT:
        case LPP_ANALOG_OUTPUT:
        case LPP_VOLTAGE: return "V";
        case LPP_LUMINOSITY: return "lux";
        case LPP_TEMPERATURE: return "\xc2\xb0""C";
        case LPP_HUMIDITY:
        case LPP_PERCENTAGE: return "%";
        case LPP_ACCELEROMETER: return "G";
        case LPP_PRESSURE: return "hPa";
        case LPP_CURRENT: return "A";
        case LPP_FREQUENCY: return "Hz";
        case LPP_ALTITUDE:
        case LPP_DISTANCE: return "m";
        case LPP_CONCENTRATION: return "ppm";
        case LPP_POWER: return "W";
        case LPP_ENERGY: return "kWh";
        case LPP_DIRECTION: return "\xc2\xb0";
        case LPP_GYROMETER: return "\xc2\xb0/s";
        default: return "";
    }
}

std::vector<LppReading> ParseCayenneLpp(const ByteBuffer& data)
{
    std::vector<LppReading> out;
    ByteReader r(data);
    // Cayenne LPP is big-endian, unlike the rest of the companion protocol.
    while (r.Remaining() >= 2) {
        LppReading v;
        v.channel = r.U8();
        v.type = r.U8();
        if (v.channel == 0 && v.type == 0)
            break;
        v.name = LppTypeName(v.type);
        v.unit = LppTypeUnit(v.type);

        switch (v.type) {
            case LPP_DIGITAL_INPUT:
            case LPP_DIGITAL_OUTPUT:
            case LPP_PRESENCE:
            case LPP_SWITCH:
            case LPP_PERCENTAGE:
                if (r.Remaining() < 1) return out;
                v.value = r.U8();
                break;
            case LPP_HUMIDITY:
                if (r.Remaining() < 1) return out;
                v.value = r.U8() / 2.0;
                break;
            case LPP_ANALOG_INPUT:
            case LPP_ANALOG_OUTPUT:
                if (r.Remaining() < 2) return out;
                v.value = r.I16BE() / 100.0;
                break;
            case LPP_TEMPERATURE:
                if (r.Remaining() < 2) return out;
                v.value = r.I16BE() / 10.0;
                break;
            case LPP_VOLTAGE:
                if (r.Remaining() < 2) return out;
                v.value = r.I16BE() / 100.0;
                break;
            case LPP_CURRENT:
                if (r.Remaining() < 2) return out;
                v.value = r.I16BE() / 1000.0;
                break;
            case LPP_ALTITUDE:
                if (r.Remaining() < 2) return out;
                v.value = r.I16BE();
                break;
            case LPP_LUMINOSITY:
            case LPP_CONCENTRATION:
            case LPP_POWER:
            case LPP_DIRECTION:
                if (r.Remaining() < 2) return out;
                v.value = r.U16BE();
                break;
            case LPP_PRESSURE:
                if (r.Remaining() < 2) return out;
                v.value = r.U16BE() / 10.0;
                break;
            case LPP_GENERIC_SENSOR:
            case LPP_FREQUENCY:
            case LPP_UNIX_TIME:
                if (r.Remaining() < 4) return out;
                v.value = r.U32BE();
                break;
            case LPP_DISTANCE:
            case LPP_ENERGY:
                if (r.Remaining() < 4) return out;
                v.value = r.U32BE() / 1000.0;
                break;
            case LPP_ACCELEROMETER:
            case LPP_GYROMETER: {
                if (r.Remaining() < 6) return out;
                double scale = (v.type == LPP_ACCELEROMETER) ? 0.001 : 0.01;
                v.is_vector = true;
                v.x = r.I16BE() * scale;
                v.y = r.I16BE() * scale;
                v.z = r.I16BE() * scale;
                break;
            }
            case LPP_COLOUR:
                if (r.Remaining() < 3) return out;
                v.is_vector = true;
                v.x = r.U8();
                v.y = r.U8();
                v.z = r.U8();
                break;
            case LPP_GPS:
                if (r.Remaining() < 9) return out;
                v.is_vector = true;
                v.x = r.I24BE() / 10000.0;  // latitude
                v.y = r.I24BE() / 10000.0;  // longitude
                v.z = r.I24BE() / 100.0;    // altitude
                break;
            default:
                // An unknown type has an unknown length, so stop here.
                return out;
        }
        if (!r.Ok())
            return out;
        out.push_back(v);
    }
    return out;
}

void ParseAdvertAppData(const ByteBuffer& buf, size_t offset, uint8_t flags, bool* has_latlon,
    double* lat, double* lon, std::string* name)
{
    *has_latlon = false;
    *lat = 0.0;
    *lon = 0.0;
    name->clear();

    if ((flags & 0x10) && buf.size() >= offset + 8) {
        ByteReader r(buf.data() + offset, 8);
        double la = static_cast<double>(r.I32()) / 1e6;
        double lo = static_cast<double>(r.I32()) / 1e6;
        // Treat the null island as "no location", like upstream.
        if (fabs(la) >= 1e-6 || fabs(lo) >= 1e-6) {
            *lat = la;
            *lon = lo;
            *has_latlon = true;
        }
        offset += 8;
    }
    if (flags & 0x20)
        offset += 2;  // feat1
    if (flags & 0x40)
        offset += 2;  // feat2
    if ((flags & 0x80) && buf.size() > offset) {
        size_t end = offset;
        while (end < buf.size() && buf[end] != 0)
            end++;
        name->assign(reinterpret_cast<const char*>(buf.data() + offset), end - offset);
    }
}

AdvertFrameInfo ParseAdvertFrame(const ByteBuffer& frame)
{
    AdvertFrameInfo a;
    // header(1) + path_len(1) + path(var) + pubkey(32) + ts(4) + sig(64) + appdata
    if (frame.size() < 2)
        return a;
    size_t i = 1;  // skip header byte
    int hop_count, hash_size;
    DecodePathLenByte(frame[i], &hop_count, &hash_size);
    i += 1;
    i += static_cast<size_t>(hop_count) * static_cast<size_t>(hash_size);

    if (i + 32 > frame.size())
        return a;
    a.pub_key_hex = ToHex(frame.data() + i, 32);
    i += 32 + 4 + 64;  // pubkey + timestamp + signature
    a.valid = true;
    if (i >= frame.size())
        return a;  // name/type absent

    uint8_t flags = frame[i];
    i += 1;
    a.type = flags & 0x0F;
    bool has_latlon;
    double lat, lon;
    ParseAdvertAppData(frame, i, flags, &has_latlon, &lat, &lon, &a.name);
    return a;
}

}  // namespace meshy
