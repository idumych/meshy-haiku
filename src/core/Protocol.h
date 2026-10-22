// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MeshCore companion-protocol constants, frame builders and response parsers.
// Ported from upstream Meshy (src/protocol.py); the wire format must stay
// byte-for-byte identical, so the layout comments here mirror that file.
//
// Parsers return a struct with a `valid` flag rather than an optional, so a
// caller can parse-then-check without exceptions.

#ifndef MESHY_CORE_PROTOCOL_H
#define MESHY_CORE_PROTOCOL_H

#include <stdint.h>

#include <string>
#include <vector>

#include "Bytes.h"

namespace meshy {

// ─── Command codes (host → companion) ────────────────────────────

enum CommandType {
    CMD_APP_START = 1,
    CMD_SEND_TXT_MSG = 2,
    CMD_SEND_CHANNEL_TXT_MSG = 3,
    CMD_GET_CONTACTS = 4,
    CMD_GET_DEVICE_TIME = 5,
    CMD_SET_DEVICE_TIME = 6,
    CMD_SEND_SELF_ADVERT = 7,
    CMD_SET_ADVERT_NAME = 8,
    CMD_ADD_UPDATE_CONTACT = 9,
    CMD_SYNC_NEXT_MESSAGE = 10,
    CMD_SET_RADIO_PARAMS = 11,
    CMD_SET_RADIO_TX_POWER = 12,
    CMD_RESET_PATH = 13,
    CMD_SET_ADVERT_LATLON = 14,
    CMD_REMOVE_CONTACT = 15,
    CMD_SHARE_CONTACT = 16,
    CMD_EXPORT_CONTACT = 17,
    CMD_IMPORT_CONTACT = 18,
    CMD_REBOOT = 19,
    CMD_GET_BATT_AND_STORAGE = 20,
    CMD_SET_TUNING_PARAMS = 21,
    CMD_DEVICE_QUERY = 22,
    CMD_EXPORT_PRIVATE_KEY = 23,
    CMD_IMPORT_PRIVATE_KEY = 24,
    CMD_SEND_RAW_DATA = 25,
    CMD_SEND_LOGIN = 26,
    CMD_SEND_STATUS_REQ = 27,
    CMD_HAS_CONNECTION = 28,
    CMD_LOGOUT = 29,
    CMD_GET_CONTACT_BY_KEY = 30,
    CMD_GET_CHANNEL = 31,
    CMD_SET_CHANNEL = 32,
    CMD_SIGN_START = 33,
    CMD_SIGN_DATA = 34,
    CMD_SIGN_FINISH = 35,
    CMD_SEND_TRACE_PATH = 36,
    CMD_SET_DEVICE_PIN = 37,
    CMD_SET_OTHER_PARAMS = 38,
    CMD_SEND_TELEMETRY_REQ = 39,
    CMD_GET_CUSTOM_VARS = 40,
    CMD_SET_CUSTOM_VAR = 41,
    CMD_GET_ADVERT_PATH = 42,
    CMD_GET_TUNING_PARAMS = 43,
    CMD_SEND_BINARY_REQ = 50,
    CMD_FACTORY_RESET = 51,
    CMD_PATH_DISCOVERY = 52,
    CMD_SET_FLOOD_SCOPE = 54,
    CMD_SEND_CONTROL_DATA = 55,
    CMD_GET_STATS = 56,
    CMD_SEND_ANON_REQ = 57,
    CMD_SET_AUTO_ADD_CONFIG = 58,
    CMD_GET_AUTO_ADD_CONFIG = 59,
    CMD_GET_ALLOWED_REPEAT_FREQ = 60,
    CMD_SET_PATH_HASH_MODE = 61,
    CMD_SET_DEFAULT_FLOOD_SCOPE = 63,
    CMD_GET_DEFAULT_FLOOD_SCOPE = 64,
    CMD_RUN_CLI_COMMAND = 66,  // v14+, local CLI on the connected companion
};

// ─── Response and push codes (companion → host) ──────────────────

enum PacketType {
    RESP_CODE_OK = 0,
    RESP_CODE_ERR = 1,
    RESP_CODE_CONTACTS_START = 2,
    RESP_CODE_CONTACT = 3,
    RESP_CODE_END_OF_CONTACTS = 4,
    RESP_CODE_SELF_INFO = 5,
    RESP_CODE_SENT = 6,
    RESP_CODE_CONTACT_MSG_RECV = 7,
    RESP_CODE_CHANNEL_MSG_RECV = 8,
    RESP_CODE_CURR_TIME = 9,
    RESP_CODE_NO_MORE_MESSAGES = 10,
    RESP_CODE_EXPORT_CONTACT = 11,
    RESP_CODE_BATT_AND_STORAGE = 12,
    RESP_CODE_DEVICE_INFO = 13,
    RESP_CODE_PRIVATE_KEY = 14,
    RESP_CODE_DISABLED = 15,
    RESP_CODE_CONTACT_MSG_RECV_V3 = 16,
    RESP_CODE_CHANNEL_MSG_RECV_V3 = 17,
    RESP_CODE_CHANNEL_INFO = 18,
    RESP_CODE_SIGN_START = 19,
    RESP_CODE_SIGNATURE = 20,
    RESP_CODE_CUSTOM_VARS = 21,
    RESP_CODE_ADVERT_PATH = 22,
    RESP_CODE_TUNING_PARAMS = 23,
    RESP_CODE_STATS = 24,
    RESP_CODE_AUTO_ADD_CONFIG = 25,
    RESP_CODE_ALLOWED_REPEAT_FREQ = 26,
    RESP_CODE_CHANNEL_DATA_RECV = 27,
    RESP_CODE_DEFAULT_FLOOD_SCOPE = 28,
    RESP_CODE_CLI_REPLY = 29,

    PUSH_CODE_ADVERT = 0x80,
    PUSH_CODE_PATH_UPDATED = 0x81,
    PUSH_CODE_SEND_CONFIRMED = 0x82,
    PUSH_CODE_MSG_WAITING = 0x83,
    PUSH_CODE_RAW_DATA = 0x84,
    PUSH_CODE_LOGIN_SUCCESS = 0x85,
    PUSH_CODE_LOGIN_FAILED = 0x86,
    PUSH_CODE_STATUS_RESPONSE = 0x87,
    PUSH_CODE_LOG_DATA = 0x88,
    PUSH_CODE_TRACE_DATA = 0x89,
    PUSH_CODE_NEW_ADVERT = 0x8A,
    PUSH_CODE_TELEMETRY_RESPONSE = 0x8B,
    RESP_CODE_BINARY_RESPONSE = 0x8C,
    PUSH_CODE_PATH_DISCOVERY_RESPONSE = 0x8D,
    RESP_CODE_CONTROL_DATA = 0x8E,
    PUSH_CODE_CONTACT_DELETED = 0x8F,
    PUSH_CODE_CONTACTS_FULL = 0x90,
};

// ─── Assorted protocol constants ─────────────────────────────────

enum TextType {
    TXT_TYPE_PLAIN = 0,
    TXT_TYPE_CLI_DATA = 1,     // CLI command or reply (repeater/room/sensor)
    TXT_TYPE_SIGNED = 2,
    TXT_TYPE_CLI_COMMAND = 3,  // explicit remote CLI command, fw >= 1.18
};

enum AdvType {
    ADV_TYPE_NONE = 0,
    ADV_TYPE_CHAT = 1,
    ADV_TYPE_REPEATER = 2,
    ADV_TYPE_ROOM = 3,
    ADV_TYPE_SENSOR = 4,
};

enum BinaryReqType {
    REQ_TYPE_STATUS = 0x01,
    REQ_TYPE_KEEP_ALIVE = 0x02,
    REQ_TYPE_TELEMETRY = 0x03,
    REQ_TYPE_MMA = 0x04,
    REQ_TYPE_ACL = 0x05,
    REQ_TYPE_GET_NEIGHBORS = 0x06,
};

enum AnonReqType {
    ANON_REQ_REGIONS = 0x01,
    ANON_REQ_OWNER = 0x02,
    ANON_REQ_BASIC = 0x03,
};

enum ControlType {
    CTRL_NODE_DISCOVER_REQ = 0x80,
    CTRL_NODE_DISCOVER_RESP = 0x90,
};

enum AutoAddFlags {
    AUTO_ADD_OVERWRITE_OLDEST = 1 << 0,
    AUTO_ADD_CHAT = 1 << 1,
    AUTO_ADD_REPEATER = 1 << 2,
    AUTO_ADD_ROOM_SERVER = 1 << 3,
    AUTO_ADD_SENSOR = 1 << 4,
    AUTO_ADD_ALL_TYPES
    = AUTO_ADD_CHAT | AUTO_ADD_REPEATER | AUTO_ADD_ROOM_SERVER | AUTO_ADD_SENSOR,
};

enum StatsType {
    STATS_TYPE_CORE = 0,
    STATS_TYPE_RADIO = 1,
    STATS_TYPE_PACKETS = 2,
};

static const size_t PUB_KEY_SIZE = 32;
static const size_t MAX_PATH_SIZE = 64;
static const size_t MAX_NAME_SIZE = 32;
static const size_t MAX_FRAME_SIZE = 172;
static const size_t MAX_TEXT_PAYLOAD = 160;
static const uint8_t APP_PROTOCOL_VERSION = 4;

// Composed length ("name: text" for channels, plain text for DMs) at or above
// which the companion's 0x88 RX-log push may no longer fit MAX_FRAME_SIZE on
// some firmwares and is silently dropped, losing heard-repeat/path metadata.
// UI warning only; sending is unaffected.
static const size_t METADATA_WARN_COMPOSED_BYTES = 139;

// Max UTF-8 bytes for a direct message.
size_t MaxDmTextBytes();
// Max UTF-8 bytes for a channel message (firmware prepends "name: ").
size_t MaxChannelTextBytes(const std::string& sender_name);

// Untranslated error strings for companion error codes; the UI translates.
const char* ErrorCodeMessage(uint8_t code);

// ─── Path-length byte helpers ────────────────────────────────────

// Raw decode: upper 2 bits hold hash_size-1, low 6 bits the hop count.  No
// validity clamping — use for byte offsets into a frame.
void DecodePathLenByte(uint8_t plen, int* hop_count, int* hash_size);
// Sanitized decode for storage: yields hop_count -1 ("unknown/flood") when the
// byte is 0xFF, the hash size is the reserved 4, or the path overruns 64 bytes.
void ParsePathLengthByte(uint8_t plen, int* hop_count, int* hash_size);
// A negative hop_count means "unknown/flood" and yields 0xFF.
uint8_t EncodePathLen(int hop_count, int hash_size = 1);

// SNR is a signed value in quarter-dB units.  Most frames carry it in one
// byte; the binary status response uses a 16-bit little-endian field.
double ParseSnr(int8_t raw);
double ParseSnr16(int16_t raw);

// Region/flood scope key: SHA-256("#name") truncated to 16 bytes.  Empty for
// the unscoped values ("", "0", "None", "*").
ByteBuffer ComputeScopeKey(const std::string& scope);
// 16-byte PSK for a hashtag channel: SHA-256 of the lowercased name.
ByteBuffer HashtagChannelPsk(const std::string& name);

// ─── Parsed-frame structs ────────────────────────────────────────

struct ContactFrame {
    bool valid = false;
    std::string public_key;    // 64 hex chars
    uint8_t type = 0;
    uint8_t flags = 0;
    int out_path_len = -1;
    int path_hash_size = 1;
    std::string out_path;      // hex, empty when unknown
    std::string adv_name;
    uint32_t last_advert = 0;
    double adv_lat = 0.0;
    double adv_lon = 0.0;
    bool has_lastmod = false;
    uint32_t lastmod = 0;
};

struct SelfInfo {
    bool valid = false;
    uint8_t adv_type = 0;
    int8_t tx_power = 0;
    uint8_t max_tx_power = 0;
    std::string public_key;
    double adv_lat = 0.0;
    double adv_lon = 0.0;
    uint8_t multi_acks = 0;
    uint8_t adv_loc_policy = 0;
    uint8_t telemetry_mode_env = 0;
    uint8_t telemetry_mode_loc = 0;
    uint8_t telemetry_mode_base = 0;
    bool manual_add_contacts = false;
    double radio_freq = 0.0;  // MHz
    double radio_bw = 0.0;    // kHz
    uint8_t radio_sf = 0;
    uint8_t radio_cr = 0;
    std::string name;
};

struct DeviceInfoFrame {
    bool valid = false;
    uint8_t fw_ver = 0;
    uint16_t max_contacts = 0;
    uint8_t max_channels = 0;
    uint32_t ble_pin = 0;
    std::string fw_build;
    std::string model;
    std::string ver;
    bool has_repeat = false;
    bool repeat = false;
    bool has_path_hash_mode = false;
    uint8_t path_hash_mode = 0;
};

struct BattAndStorage {
    bool valid = false;
    uint16_t level = 0;
    bool has_storage = false;
    uint32_t used_kb = 0;
    uint32_t total_kb = 0;
};

struct ContactMessage {
    bool valid = false;
    bool has_snr = false;
    double snr = 0.0;
    std::string pubkey_prefix;  // 12 hex chars
    int path_len = -1;
    int path_hash_size = 1;
    uint8_t txt_type = 0;
    uint32_t sender_timestamp = 0;
    std::string signature;  // hex, only for TXT_TYPE_SIGNED
    std::string text;
};

struct ChannelMessageFrame {
    bool valid = false;
    bool has_snr = false;
    double snr = 0.0;
    uint8_t channel_idx = 0;
    int path_len = -1;
    int path_hash_size = 1;
    uint8_t txt_type = 0;
    uint32_t sender_timestamp = 0;
    std::string text;         // raw "name: message"
    std::string sender_name;
    std::string msg_text;     // smaz-decoded message body
};

struct ChannelData {
    bool valid = false;
    double snr = 0.0;
    uint8_t channel_idx = 0;
    int path_len = -1;
    int path_hash_size = 1;
    uint16_t data_type = 0;
    uint8_t data_len = 0;
    std::string payload;  // hex
};

struct ChannelInfo {
    bool valid = false;
    uint8_t channel_idx = 0;
    std::string channel_name;
    ByteBuffer channel_secret;  // 16 bytes
};

struct SentResponse {
    bool valid = false;
    uint8_t type = 0;
    ByteBuffer expected_ack;  // 4 bytes
    uint32_t suggested_timeout = 0;
};

struct SendConfirmed {
    bool valid = false;
    std::string code;  // 4-byte ack hex
    bool has_trip_time = false;
    uint32_t trip_time_ms = 0;
};

struct AutoAddConfig {
    bool valid = false;
    uint8_t config = 0;
    uint8_t max_hops = 0;
};

struct DefaultFloodScope {
    bool valid = false;
    bool has_scope = false;
    std::string scope_name;
    std::string scope_key;  // hex
};

struct ErrorResponse {
    bool valid = false;
    uint8_t code = 0;
    std::string message;
};

struct AdvertPathResponse {
    bool valid = false;
    int path_length = -1;
    int path_hash_size = 1;
    ByteBuffer path;
    uint32_t timestamp = 0;
    std::vector<std::string> hops;  // uppercase hex per hop
};

struct TraceResponse {
    bool valid = false;
    ByteBuffer tag;  // 4 bytes
    std::vector<ByteBuffer> path_hops;
    int path_hash_size = 1;
    std::vector<double> snr_values;
};

struct StatusResponse {
    bool valid = false;
    uint16_t battery_mv = 0;
    uint16_t queue_len = 0;
    int16_t noise_floor = 0;
    int16_t last_rssi = 0;
    uint32_t packets_recv = 0;
    uint32_t packets_sent = 0;
    uint32_t tx_air_secs = 0;
    uint32_t uptime_secs = 0;
    uint32_t flood_tx = 0;
    uint32_t direct_tx = 0;
    uint32_t flood_rx = 0;
    uint32_t direct_rx = 0;
    uint16_t error_events = 0;
    double last_snr = 0.0;
    uint16_t dup_direct = 0;
    uint16_t dup_flood = 0;
    uint32_t rx_air_secs = 0;
    bool has_recv_errors = false;
    uint32_t recv_errors = 0;
};

struct NeighborEntry {
    std::string pub_key_hex;
    uint32_t last_heard = 0;
    double snr = 0.0;
};

struct NeighborsResponse {
    uint16_t total = 0;
    std::vector<NeighborEntry> neighbors;
};

struct DiscoverResponse {
    bool valid = false;
    uint8_t node_type = 0;
    double snr = 0.0;
    uint32_t tag = 0;
    std::string pub_key_hex;
};

struct StatsResponse {
    bool valid = false;
    uint8_t sub_type = 0;
    // Core
    uint16_t battery_mv = 0;
    uint32_t uptime_secs = 0;
    uint16_t errors = 0;
    uint8_t queue_len = 0;
    // Radio
    int16_t noise_floor = 0;
    int8_t last_rssi = 0;
    double last_snr = 0.0;
    uint32_t tx_air_secs = 0;
    uint32_t rx_air_secs = 0;
    // Packets
    uint32_t recv = 0;
    uint32_t sent = 0;
    uint32_t flood_tx = 0;
    uint32_t direct_tx = 0;
    uint32_t flood_rx = 0;
    uint32_t direct_rx = 0;
    bool has_recv_errors = false;
    uint32_t recv_errors = 0;
};

struct AdvertFrameInfo {
    bool valid = false;
    std::string pub_key_hex;
    std::string name;
    uint8_t type = 0;
};

struct FreqRange {
    uint32_t lower_khz = 0;
    uint32_t upper_khz = 0;
};

// ─── Frame builders ──────────────────────────────────────────────
//
// A timestamp of 0 means "use the current time".

ByteBuffer BuildAppStart(const std::string& app_name = "Meshy",
    uint8_t app_version = APP_PROTOCOL_VERSION);
ByteBuffer BuildDeviceQuery(uint8_t app_version = APP_PROTOCOL_VERSION);
ByteBuffer BuildGetContacts();
ByteBuffer BuildGetContactsSince(uint32_t since);
ByteBuffer BuildGetDeviceTime();
ByteBuffer BuildSetDeviceTime(uint32_t timestamp = 0);
ByteBuffer BuildGetBattAndStorage();
ByteBuffer BuildSyncNextMessage();

ByteBuffer BuildSendTextMsg(const ByteBuffer& recipient_pub_key, const std::string& text,
    uint8_t attempt = 0, uint32_t timestamp = 0);
ByteBuffer BuildSendChannelTextMsg(uint8_t channel_index, const std::string& text,
    uint32_t timestamp = 0);
// Companion chats need TXT_TYPE_CLI_COMMAND (fw >= 1.18); repeaters, rooms and
// sensors still use TXT_TYPE_CLI_DATA.
uint8_t CliTxtType(int contact_type, bool supports_cli);
ByteBuffer BuildSendCliCommand(const ByteBuffer& pub_key, const std::string& command,
    uint8_t attempt = 0, uint32_t timestamp = 0, uint8_t txt_type = TXT_TYPE_CLI_DATA);
// The firmware null-terminates this one itself, so no trailing NUL is added.
ByteBuffer BuildRunCliCommand(const std::string& command);

ByteBuffer BuildSendSelfAdvert(bool flood = false);
ByteBuffer BuildSetAdvertName(const std::string& name);
ByteBuffer BuildSetAdvertLatLon(double lat, double lon);

ByteBuffer BuildRemoveContact(const ByteBuffer& pub_key);
ByteBuffer BuildAddUpdateContact(const ByteBuffer& pub_key, uint8_t contact_type = ADV_TYPE_CHAT,
    uint8_t flags = 0, uint8_t path_len = 0, const ByteBuffer& path = ByteBuffer(),
    const std::string& name = std::string(), bool has_location = false, double lat = 0.0,
    double lon = 0.0, bool has_last_modified = false, uint32_t last_modified = 0,
    uint32_t last_advert = 0);
ByteBuffer BuildGetContactByKey(const ByteBuffer& pub_key);
ByteBuffer BuildResetPath(const ByteBuffer& pub_key);
ByteBuffer BuildShareContact(const ByteBuffer& pub_key);
ByteBuffer BuildExportContact(const ByteBuffer& pub_key);
ByteBuffer BuildImportContact(const ByteBuffer& contact_frame);

ByteBuffer BuildGetChannel(uint8_t channel_index);
ByteBuffer BuildSetChannel(uint8_t channel_index, const std::string& name, const ByteBuffer& psk);

ByteBuffer BuildSetRadioParams(uint32_t freq_khz, uint32_t bw_khz, uint8_t sf, uint8_t cr);
ByteBuffer BuildSetRadioParamsRepeat(uint32_t freq_khz, uint32_t bw_khz, uint8_t sf, uint8_t cr,
    bool client_repeat);
ByteBuffer BuildSetRadioTxPower(int power_dbm);
ByteBuffer BuildReboot();
ByteBuffer BuildFactoryReset();
ByteBuffer BuildSetPathHashMode(uint8_t mode);  // 0=1-byte, 1=2-byte, 2=3-byte
ByteBuffer BuildSetDevicePin(uint32_t pin);
ByteBuffer BuildSetOtherParams(uint8_t manual_add_contacts = 0, uint8_t telemetry_flags = 0,
    uint8_t advert_loc_policy = 0, uint8_t multi_acks = 0);

ByteBuffer BuildGetCustomVars();
ByteBuffer BuildSetCustomVar(const std::string& key_value);  // "key:value"

ByteBuffer BuildSetFloodScope(const std::string& scope = std::string(), bool unscoped = false);
ByteBuffer BuildSetDefaultFloodScope(const std::string& scope = std::string());
ByteBuffer BuildGetDefaultFloodScope();

ByteBuffer BuildGetAdvertPath(const ByteBuffer& pub_key);
ByteBuffer BuildPathDiscovery(const ByteBuffer& pub_key);
ByteBuffer BuildTracePath(uint32_t tag, const ByteBuffer& path_bytes, uint8_t flags = 0);

ByteBuffer BuildSendLogin(const ByteBuffer& recipient_pub_key, const std::string& password);
ByteBuffer BuildLogout(const ByteBuffer& pub_key);
ByteBuffer BuildSendStatusReq(const ByteBuffer& recipient_pub_key);

ByteBuffer BuildGetAutoAddConfig();
ByteBuffer BuildSetAutoAddConfig(bool chat, bool repeater, bool room, bool sensor,
    bool overwrite_oldest, uint8_t max_hops);

// tag == 0 draws a random correlation tag.
ByteBuffer BuildSendDiscoverReq(uint8_t type_filter = 0xFF, bool prefix_only = true,
    uint32_t tag = 0);
ByteBuffer BuildGetNeighbors(const ByteBuffer& repeater_pub_key, uint8_t max_results = 10,
    uint16_t offset = 0, uint8_t key_len = 4);
ByteBuffer BuildGetAcl(const ByteBuffer& repeater_pub_key);
ByteBuffer BuildGetTelemetry(const ByteBuffer& repeater_pub_key);
// Empty pub_key requests self-telemetry from the connected device.
ByteBuffer BuildGetTelemetryChat(const ByteBuffer& pub_key = ByteBuffer());

ByteBuffer BuildAnonReqRegions(const ByteBuffer& pub_key, int path_length, const ByteBuffer& path,
    int path_hash_size = 1);
ByteBuffer BuildAnonReqOwner(const ByteBuffer& pub_key, int path_length, const ByteBuffer& path,
    int path_hash_size = 1);

ByteBuffer BuildGetAllowedRepeatFreq();
ByteBuffer BuildGetStats(uint8_t sub_type = STATS_TYPE_CORE);
ByteBuffer BuildExportPrivateKey();
ByteBuffer BuildImportPrivateKey(const ByteBuffer& key);

// ─── Frame parsers ───────────────────────────────────────────────

// Returns the leading code byte, or -1 for an empty frame.
int IdentifyResponse(const ByteBuffer& data);

ContactFrame ParseContactFrame(const ByteBuffer& data);
SelfInfo ParseSelfInfo(const ByteBuffer& data);
DeviceInfoFrame ParseDeviceInfo(const ByteBuffer& data);
BattAndStorage ParseBattAndStorage(const ByteBuffer& data);
ContactMessage ParseContactMessage(const ByteBuffer& data);
ChannelMessageFrame ParseChannelMessage(const ByteBuffer& data);
ChannelData ParseChannelData(const ByteBuffer& data);
ChannelInfo ParseChannelInfo(const ByteBuffer& data);
SentResponse ParseSentResponse(const ByteBuffer& data);
SendConfirmed ParsePushSendConfirmed(const ByteBuffer& data);
AutoAddConfig ParseAutoAddConfig(const ByteBuffer& data);
DefaultFloodScope ParseDefaultFloodScope(const ByteBuffer& data);
ErrorResponse ParseErrorResponse(const ByteBuffer& data);
AdvertPathResponse ParseAdvertPathResponse(const ByteBuffer& data);
TraceResponse ParseTraceResponse(const ByteBuffer& data);
StatusResponse ParseStatusResponse(const ByteBuffer& data);
NeighborsResponse ParseNeighborsResponse(const ByteBuffer& data, uint8_t key_len = 4);
DiscoverResponse ParseDiscoverResponse(const ByteBuffer& data);
StatsResponse ParseStatsResponse(const ByteBuffer& data);
std::vector<FreqRange> ParseAllowedRepeatFreq(const ByteBuffer& data);
std::vector<std::pair<std::string, std::string> > ParseCustomVars(const ByteBuffer& data);
// Returns false when the frame is not a CURRENT_TIME response.
bool ParseCurrTime(const ByteBuffer& data, uint32_t* out_time);
// Returns false when the frame is not a CLI reply.
bool ParseCliReply(const ByteBuffer& data, std::string* out_text);

// ─── Cayenne LPP telemetry ───────────────────────────────────────

enum LppType {
    LPP_DIGITAL_INPUT = 0,
    LPP_DIGITAL_OUTPUT = 1,
    LPP_ANALOG_INPUT = 2,
    LPP_ANALOG_OUTPUT = 3,
    LPP_GENERIC_SENSOR = 100,
    LPP_LUMINOSITY = 101,
    LPP_PRESENCE = 102,
    LPP_TEMPERATURE = 103,
    LPP_HUMIDITY = 104,
    LPP_ACCELEROMETER = 113,
    LPP_PRESSURE = 115,
    LPP_VOLTAGE = 116,
    LPP_CURRENT = 117,
    LPP_FREQUENCY = 118,
    LPP_PERCENTAGE = 120,
    LPP_ALTITUDE = 121,
    LPP_CONCENTRATION = 125,
    LPP_POWER = 128,
    LPP_DISTANCE = 130,
    LPP_ENERGY = 131,
    LPP_DIRECTION = 132,
    LPP_UNIX_TIME = 133,
    LPP_GYROMETER = 134,
    LPP_COLOUR = 135,
    LPP_GPS = 136,
    LPP_SWITCH = 142,
};

// One decoded reading.  Most types carry a single scalar in `value`; the
// vector types (accelerometer, gyrometer, colour, GPS) fill `x`, `y` and `z`
// instead and set `is_vector`.
struct LppReading {
    uint8_t channel = 0;
    uint8_t type = 0;
    const char* name = "";
    const char* unit = "";
    bool is_vector = false;
    double value = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

const char* LppTypeName(uint8_t type);
const char* LppTypeUnit(uint8_t type);
// Stops at the first unknown type, since its length is then unknown.
std::vector<LppReading> ParseCayenneLpp(const ByteBuffer& data);

// Trailing app-data of an advert, starting just after the flags byte.
// lat/lon are reported only when present and not at the null island.
void ParseAdvertAppData(const ByteBuffer& buf, size_t offset, uint8_t flags, bool* has_latlon,
    double* lat, double* lon, std::string* name);
AdvertFrameInfo ParseAdvertFrame(const ByteBuffer& frame);

}  // namespace meshy

#endif  // MESHY_CORE_PROTOCOL_H
