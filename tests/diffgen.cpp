// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Differential test generator.  Emits "name|value" lines for a deterministic
// battery of protocol cases.  tests/diffgen.py produces the same lines using
// the upstream Python implementation; any diff is a port bug.

#include <cstdio>
#include <string>
#include <vector>

#include "Bytes.h"
#include "Crypto.h"
#include "MeshPacket.h"
#include "Protocol.h"
#include "Smaz.h"

using namespace meshy;

// Deterministic PRNG, mirrored exactly in the Python generator.
struct Lcg {
    uint32_t state;
    explicit Lcg(uint32_t seed) : state(seed) {}
    uint32_t Next()
    {
        state = (state * 1103515245u + 12345u) & 0x7FFFFFFFu;
        return state;
    }
    uint8_t Byte() { return static_cast<uint8_t>((Next() >> 16) & 0xFF); }
    uint32_t Below(uint32_t n) { return n ? Next() % n : 0; }
    ByteBuffer Bytes(size_t n)
    {
        ByteBuffer b;
        b.reserve(n);
        for (size_t i = 0; i < n; i++)
            b.push_back(Byte());
        return b;
    }
};

static void Emit(const std::string& name, const std::string& value)
{
    printf("%s|%s\n", name.c_str(), value.c_str());
}

static void EmitFrame(const std::string& name, const ByteBuffer& f)
{
    Emit(name, ToHex(f));
}

static std::string F(double v)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.6f", v);
    return buf;
}

static std::string I(long long v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", v);
    return buf;
}

static std::string B(bool v)
{
    return v ? "1" : "0";
}

// Escapes text so a value never contains a newline or pipe.
static std::string Q(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c == '|' || c == '\\' || c >= 0x7F) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\x%02x", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

// ─── Builders ────────────────────────────────────────────────────

static void TestBuilders()
{
    const uint32_t kTs = 1760000000u;
    Lcg rng(12345);

    EmitFrame("app_start.default", BuildAppStart("Meshy", APP_PROTOCOL_VERSION));
    EmitFrame("app_start.named", BuildAppStart("HaikuMeshy", 7));
    EmitFrame("app_start.utf8", BuildAppStart(u8"Méšy", 4));
    EmitFrame("device_query.default", BuildDeviceQuery());
    EmitFrame("device_query.v9", BuildDeviceQuery(9));
    EmitFrame("get_contacts", BuildGetContacts());
    EmitFrame("get_contacts.since", BuildGetContactsSince(1700000123u));
    EmitFrame("get_device_time", BuildGetDeviceTime());
    EmitFrame("set_device_time", BuildSetDeviceTime(kTs));
    EmitFrame("get_batt", BuildGetBattAndStorage());
    EmitFrame("sync_next", BuildSyncNextMessage());

    ByteBuffer key32 = rng.Bytes(32);
    Emit("fixture.key32", ToHex(key32));

    EmitFrame("send_text.basic", BuildSendTextMsg(key32, "hello world", 0, kTs));
    EmitFrame("send_text.attempt", BuildSendTextMsg(key32, "retry", 3, kTs + 5));
    EmitFrame("send_text.attempt_clamp", BuildSendTextMsg(key32, "clamp", 255, kTs));
    EmitFrame("send_text.utf8", BuildSendTextMsg(key32, u8"příliš žlu", 0, kTs));
    EmitFrame("send_text.empty", BuildSendTextMsg(key32, "", 0, kTs));

    EmitFrame("send_chan.basic", BuildSendChannelTextMsg(0, "chan msg", kTs));
    EmitFrame("send_chan.idx7", BuildSendChannelTextMsg(7, "another", kTs));

    Emit("cli_txt_type.chat_cli", I(CliTxtType(ADV_TYPE_CHAT, true)));
    Emit("cli_txt_type.chat_nocli", I(CliTxtType(ADV_TYPE_CHAT, false)));
    Emit("cli_txt_type.repeater", I(CliTxtType(ADV_TYPE_REPEATER, true)));
    EmitFrame("send_cli.data", BuildSendCliCommand(key32, "ver", 0, kTs, TXT_TYPE_CLI_DATA));
    EmitFrame("send_cli.cmd", BuildSendCliCommand(key32, "reboot", 1, kTs, TXT_TYPE_CLI_COMMAND));
    EmitFrame("run_cli", BuildRunCliCommand("get freq"));

    EmitFrame("self_advert.zero", BuildSendSelfAdvert(false));
    EmitFrame("self_advert.flood", BuildSendSelfAdvert(true));
    EmitFrame("set_advert_name.short", BuildSetAdvertName("node"));
    EmitFrame("set_advert_name.long",
        BuildSetAdvertName("abcdefghijklmnopqrstuvwxyz0123456789"));
    // 31-byte limit must not split a multi-byte sequence.
    EmitFrame("set_advert_name.utf8trunc",
        BuildSetAdvertName(u8"žluťoučký kůň úpěl ďábelské ódy"));
    EmitFrame("set_latlon.pos", BuildSetAdvertLatLon(50.0755, 14.4378));
    EmitFrame("set_latlon.neg", BuildSetAdvertLatLon(-33.8688, -151.2093));
    EmitFrame("set_latlon.zero", BuildSetAdvertLatLon(0.0, 0.0));

    EmitFrame("remove_contact", BuildRemoveContact(key32));
    EmitFrame("get_contact_by_key", BuildGetContactByKey(key32));
    EmitFrame("reset_path", BuildResetPath(key32));
    EmitFrame("share_contact", BuildShareContact(key32));
    EmitFrame("export_contact", BuildExportContact(key32));
    EmitFrame("import_contact", BuildImportContact(rng.Bytes(40)));

    ByteBuffer path3 = rng.Bytes(3);
    Emit("fixture.path3", ToHex(path3));
    EmitFrame("add_contact.minimal",
        BuildAddUpdateContact(key32, ADV_TYPE_CHAT, 0, 0, ByteBuffer(), "bob", false, 0, 0, false,
            0, kTs));
    EmitFrame("add_contact.path",
        BuildAddUpdateContact(key32, ADV_TYPE_REPEATER, 2, 3, path3, "rep1", false, 0, 0, false, 0,
            kTs));
    EmitFrame("add_contact.loc",
        BuildAddUpdateContact(key32, ADV_TYPE_ROOM, 0, 0, ByteBuffer(), "room", true, 50.1, 14.2,
            false, 0, kTs));
    EmitFrame("add_contact.loc_lastmod",
        BuildAddUpdateContact(key32, ADV_TYPE_SENSOR, 1, 3, path3, "sens", true, -12.5, 130.75,
            true, kTs + 99, kTs));
    EmitFrame("add_contact.lastmod_only",
        BuildAddUpdateContact(key32, ADV_TYPE_CHAT, 0, 0, ByteBuffer(), "x", false, 0, 0, true,
            kTs + 7, kTs));

    EmitFrame("get_channel", BuildGetChannel(3));
    EmitFrame("set_channel.basic", BuildSetChannel(0, "general", rng.Bytes(16)));
    EmitFrame("set_channel.shortpsk", BuildSetChannel(1, "short", rng.Bytes(4)));
    EmitFrame("set_channel.longname",
        BuildSetChannel(2, "a-very-long-channel-name-that-overflows", rng.Bytes(16)));

    EmitFrame("radio_params", BuildSetRadioParams(869525, 250, 11, 5));
    EmitFrame("radio_params.repeat_on", BuildSetRadioParamsRepeat(915000, 125, 7, 8, true));
    EmitFrame("radio_params.repeat_off", BuildSetRadioParamsRepeat(915000, 125, 7, 8, false));
    EmitFrame("tx_power.pos", BuildSetRadioTxPower(22));
    EmitFrame("tx_power.neg", BuildSetRadioTxPower(-5));
    EmitFrame("reboot", BuildReboot());
    EmitFrame("factory_reset", BuildFactoryReset());
    EmitFrame("path_hash_mode", BuildSetPathHashMode(2));
    EmitFrame("device_pin", BuildSetDevicePin(123456));
    EmitFrame("other_params", BuildSetOtherParams(1, 0x15, 2, 3));

    EmitFrame("get_custom_vars", BuildGetCustomVars());
    EmitFrame("set_custom_var", BuildSetCustomVar("gps:1"));

    EmitFrame("flood_scope.empty", BuildSetFloodScope("", false));
    EmitFrame("flood_scope.star", BuildSetFloodScope("*", false));
    EmitFrame("flood_scope.named", BuildSetFloodScope("czechia", false));
    EmitFrame("flood_scope.hash", BuildSetFloodScope("#czechia", false));
    EmitFrame("flood_scope.unscoped", BuildSetFloodScope("#eu", true));
    EmitFrame("default_scope.clear", BuildSetDefaultFloodScope(""));
    EmitFrame("default_scope.named", BuildSetDefaultFloodScope("#eu-region"));
    EmitFrame("default_scope.nohash", BuildSetDefaultFloodScope("eu-region"));
    EmitFrame("default_scope.get", BuildGetDefaultFloodScope());

    EmitFrame("get_advert_path", BuildGetAdvertPath(key32));
    EmitFrame("path_discovery", BuildPathDiscovery(key32));
    EmitFrame("trace_path", BuildTracePath(0xDEADBEEF, rng.Bytes(4), 1));

    EmitFrame("send_login", BuildSendLogin(key32, "hunter2"));
    EmitFrame("logout", BuildLogout(key32));
    EmitFrame("status_req", BuildSendStatusReq(key32));

    EmitFrame("get_autoadd", BuildGetAutoAddConfig());
    EmitFrame("set_autoadd.all",
        BuildSetAutoAddConfig(true, true, true, true, true, 5));
    EmitFrame("set_autoadd.chat", BuildSetAutoAddConfig(true, false, false, false, false, 0));
    EmitFrame("set_autoadd.clamp", BuildSetAutoAddConfig(false, true, false, false, false, 200));

    EmitFrame("discover_req", BuildSendDiscoverReq(0xFF, true, 0x11223344));
    EmitFrame("discover_req.full", BuildSendDiscoverReq(0x06, false, 0x55667788));
    EmitFrame("get_neighbors", BuildGetNeighbors(key32, 10, 0, 4));
    EmitFrame("get_neighbors.off", BuildGetNeighbors(key32, 5, 300, 6));
    EmitFrame("get_acl", BuildGetAcl(key32));
    EmitFrame("get_telemetry_chat.self", BuildGetTelemetryChat());
    EmitFrame("get_telemetry_chat.peer", BuildGetTelemetryChat(key32));

    ByteBuffer path6 = rng.Bytes(6);
    Emit("fixture.path6", ToHex(path6));
    EmitFrame("anon_regions", BuildAnonReqRegions(key32, 3, path6, 2));
    EmitFrame("anon_owner", BuildAnonReqOwner(key32, 6, path6, 1));
    EmitFrame("anon_regions.clamp", BuildAnonReqRegions(key32, 99, path6, 1));

    EmitFrame("allowed_freq", BuildGetAllowedRepeatFreq());
    EmitFrame("stats.core", BuildGetStats(STATS_TYPE_CORE));
    EmitFrame("stats.radio", BuildGetStats(STATS_TYPE_RADIO));
    EmitFrame("export_privkey", BuildExportPrivateKey());
    EmitFrame("import_privkey", BuildImportPrivateKey(rng.Bytes(64)));
    EmitFrame("import_privkey.long", BuildImportPrivateKey(rng.Bytes(80)));

    Emit("max_dm_text", I(static_cast<long long>(MaxDmTextBytes())));
    Emit("max_chan_text.empty", I(static_cast<long long>(MaxChannelTextBytes(""))));
    Emit("max_chan_text.name", I(static_cast<long long>(MaxChannelTextBytes("alice"))));
    Emit("max_chan_text.long",
        I(static_cast<long long>(MaxChannelTextBytes(std::string(100, 'x')))));
}

// ─── Path length + scope helpers ─────────────────────────────────

static void TestHelpers()
{
    for (int plen = 0; plen < 256; plen++) {
        int hc, hs;
        DecodePathLenByte(static_cast<uint8_t>(plen), &hc, &hs);
        int phc, phs;
        ParsePathLengthByte(static_cast<uint8_t>(plen), &phc, &phs);
        Emit("pathlen." + I(plen),
            I(hc) + "," + I(hs) + "," + I(phc) + "," + I(phs));
    }
    for (int hc = -2; hc < 70; hc++) {
        for (int hs = 0; hs < 5; hs++)
            Emit("encpath." + I(hc) + "." + I(hs), I(EncodePathLen(hc, hs)));
    }
    for (int i = -128; i < 128; i++)
        Emit("snr." + I(i), F(ParseSnr(static_cast<int8_t>(i))));

    const char* scopes[] = { "", "0", "None", "*", "eu", "#eu", "#Czechia", "$private", " spaced ",
        u8"éregion" };
    for (size_t i = 0; i < sizeof(scopes) / sizeof(scopes[0]); i++) {
        Emit("scopekey." + I(static_cast<long long>(i)), ToHex(ComputeScopeKey(scopes[i])));
        ByteBuffer dk;
        bool ok = DeriveScopeKey(scopes[i], &dk);
        Emit("derivescope." + I(static_cast<long long>(i)), ok ? ToHex(dk) : std::string("none"));
    }
    const char* chans[] = { "#test", "#TEST", "general", "", u8"#čeština" };
    for (size_t i = 0; i < sizeof(chans) / sizeof(chans[0]); i++)
        Emit("hashtagpsk." + I(static_cast<long long>(i)), ToHex(HashtagChannelPsk(chans[i])));

    // Channel hash and transport codes.
    Lcg rng(777);
    for (int i = 0; i < 8; i++) {
        ByteBuffer psk = rng.Bytes(16);
        Emit("chanhash." + I(i), ToHex(psk) + "=" + ChannelHashBytes(psk));
    }
    std::vector<std::string> regions;
    regions.push_back("eu");
    regions.push_back("#eu");
    regions.push_back("us");
    regions.push_back("$priv");
    for (int i = 0; i < 6; i++) {
        ByteBuffer payload = rng.Bytes(1 + rng.Below(40));
        uint8_t ptype = static_cast<uint8_t>(rng.Below(16));
        ByteBuffer k;
        DeriveScopeKey("eu", &k);
        uint16_t code = CalcTransportCode(k, ptype, payload);
        Emit("tcode." + I(i), ToHex(payload) + "/" + I(ptype) + "=" + I(code));
        Emit("region." + I(i), ResolveRegionName(code, ptype, payload, regions));
    }

    // SMAZ round-trip is one-way here: decode known prefixed payloads.
    const char* smaz_cases[] = { "plain text", "s:", "s:AQID", "s:dGhlIHF1aWNr", "  s:AQID",
        "s:!!!!", "s:-_8A", "not s: prefixed" };
    for (size_t i = 0; i < sizeof(smaz_cases) / sizeof(smaz_cases[0]); i++)
        Emit("smaz." + I(static_cast<long long>(i)), Q(smaz::TryDecodePrefixed(smaz_cases[i])));
}

// ─── Parsers ─────────────────────────────────────────────────────

static std::string DumpContact(const ContactFrame& c)
{
    if (!c.valid)
        return "invalid";
    return "pk=" + c.public_key + ";type=" + I(c.type) + ";flags=" + I(c.flags) + ";plen="
        + I(c.out_path_len) + ";hs=" + I(c.path_hash_size) + ";path=" + c.out_path + ";name="
        + Q(c.adv_name) + ";adv=" + I(c.last_advert) + ";lat=" + F(c.adv_lat) + ";lon="
        + F(c.adv_lon) + ";lastmod=" + (c.has_lastmod ? I(c.lastmod) : std::string("-"));
}

static std::string DumpSelfInfo(const SelfInfo& s)
{
    if (!s.valid)
        return "invalid";
    return "advtype=" + I(s.adv_type) + ";tx=" + I(s.tx_power) + ";maxtx=" + I(s.max_tx_power)
        + ";pk=" + s.public_key + ";lat=" + F(s.adv_lat) + ";lon=" + F(s.adv_lon) + ";macks="
        + I(s.multi_acks) + ";locpol=" + I(s.adv_loc_policy) + ";tenv=" + I(s.telemetry_mode_env)
        + ";tloc=" + I(s.telemetry_mode_loc) + ";tbase=" + I(s.telemetry_mode_base) + ";manual="
        + B(s.manual_add_contacts) + ";freq=" + F(s.radio_freq) + ";bw=" + F(s.radio_bw) + ";sf="
        + I(s.radio_sf) + ";cr=" + I(s.radio_cr) + ";name=" + Q(s.name);
}

static std::string DumpDeviceInfo(const DeviceInfoFrame& d)
{
    if (!d.valid)
        return "invalid";
    return "fw=" + I(d.fw_ver) + ";maxc=" + I(d.max_contacts) + ";maxch=" + I(d.max_channels)
        + ";pin=" + I(d.ble_pin) + ";build=" + Q(d.fw_build) + ";model=" + Q(d.model) + ";ver="
        + Q(d.ver) + ";repeat=" + (d.has_repeat ? B(d.repeat) : std::string("-")) + ";phm="
        + (d.has_path_hash_mode ? I(d.path_hash_mode) : std::string("-"));
}

static std::string DumpContactMsg(const ContactMessage& m)
{
    if (!m.valid)
        return "invalid";
    return "snr=" + (m.has_snr ? F(m.snr) : std::string("-")) + ";prefix=" + m.pubkey_prefix
        + ";plen=" + I(m.path_len) + ";hs=" + I(m.path_hash_size) + ";txt=" + I(m.txt_type)
        + ";ts=" + I(m.sender_timestamp) + ";sig=" + (m.signature.empty() ? "-" : m.signature)
        + ";text=" + Q(m.text);
}

static std::string DumpChannelMsg(const ChannelMessageFrame& m)
{
    if (!m.valid)
        return "invalid";
    return "snr=" + (m.has_snr ? F(m.snr) : std::string("-")) + ";idx=" + I(m.channel_idx)
        + ";plen=" + I(m.path_len) + ";hs=" + I(m.path_hash_size) + ";txt=" + I(m.txt_type) + ";ts="
        + I(m.sender_timestamp) + ";text=" + Q(m.text) + ";sender=" + Q(m.sender_name) + ";body="
        + Q(m.msg_text);
}

static std::string DumpStatus(const StatusResponse& s)
{
    if (!s.valid)
        return "invalid";
    return "batt=" + I(s.battery_mv) + ";q=" + I(s.queue_len) + ";nf=" + I(s.noise_floor)
        + ";rssi=" + I(s.last_rssi) + ";prx=" + I(s.packets_recv) + ";ptx=" + I(s.packets_sent)
        + ";tx=" + I(s.tx_air_secs) + ";up=" + I(s.uptime_secs) + ";ftx=" + I(s.flood_tx) + ";dtx="
        + I(s.direct_tx) + ";frx=" + I(s.flood_rx) + ";drx=" + I(s.direct_rx) + ";err="
        + I(s.error_events) + ";snr=" + F(s.last_snr) + ";dd=" + I(s.dup_direct) + ";df="
        + I(s.dup_flood) + ";rxair=" + I(s.rx_air_secs) + ";rxerr="
        + (s.has_recv_errors ? I(s.recv_errors) : std::string("-"));
}

static std::string DumpStats(const StatsResponse& s)
{
    if (!s.valid)
        return "invalid";
    std::string out = "sub=" + I(s.sub_type);
    if (s.sub_type == STATS_TYPE_CORE)
        out += ";batt=" + I(s.battery_mv) + ";up=" + I(s.uptime_secs) + ";err=" + I(s.errors)
            + ";q=" + I(s.queue_len);
    else if (s.sub_type == STATS_TYPE_RADIO)
        out += ";nf=" + I(s.noise_floor) + ";rssi=" + I(s.last_rssi) + ";snr=" + F(s.last_snr)
            + ";tx=" + I(s.tx_air_secs) + ";rx=" + I(s.rx_air_secs);
    else if (s.sub_type == STATS_TYPE_PACKETS)
        out += ";recv=" + I(s.recv) + ";sent=" + I(s.sent) + ";ftx=" + I(s.flood_tx) + ";dtx="
            + I(s.direct_tx) + ";frx=" + I(s.flood_rx) + ";drx=" + I(s.direct_rx) + ";rxerr="
            + (s.has_recv_errors ? I(s.recv_errors) : std::string("-"));
    return out;
}

static std::string DumpLogPacket(const LogPacket& p)
{
    if (!p.valid)
        return "invalid";
    std::string out = "ptype=" + I(p.payload_type) + ";rtype=" + I(p.route_type) + ";hs="
        + I(p.path_hash_size) + ";tfrom=" + (p.transport_from.empty() ? "-" : p.transport_from)
        + ";tto=" + (p.transport_to.empty() ? "-" : p.transport_to) + ";hash=" + p.pkt_hash
        + ";payload=" + ToHex(p.payload) + ";path=";
    for (size_t i = 0; i < p.path.size(); i++) {
        if (i)
            out += ",";
        out += p.path[i];
    }
    if (p.has_advert) {
        out += ";advert=" + p.advert.public_key + "/" + I(p.advert.type) + "/"
            + Q(p.advert.adv_name) + "/" + I(p.advert.last_advert) + "/"
            + (p.advert.has_location ? F(p.advert.adv_lat) + "," + F(p.advert.adv_lon)
                                     : std::string("-"));
    }
    if (p.has_channel)
        out += ";chan=" + p.channel_hash + "/" + ToHex(p.ciphertext);
    return out;
}

static void TestParsers()
{
    Lcg rng(999);

    // Well-formed contact frames with random field values.
    for (int i = 0; i < 12; i++) {
        ByteWriter w;
        w.U8(i % 2 ? RESP_CODE_CONTACT : PUSH_CODE_NEW_ADVERT);
        w.Bytes(rng.Bytes(32));
        w.U8(static_cast<uint8_t>(rng.Below(5)));
        w.U8(rng.Byte());
        w.U8(rng.Byte());  // path length byte, often nonsense on purpose
        w.Bytes(rng.Bytes(64));
        w.StrTruncated("node-" + I(i), 31, 32);
        w.U32(1700000000u + i);
        w.I32(static_cast<int32_t>(rng.Next() % 90000000) - 45000000);
        w.I32(static_cast<int32_t>(rng.Next() % 90000000) - 45000000);
        if (i % 3 == 0)
            w.U32(1700000500u + i);
        ByteBuffer f = w.Take();
        Emit("in.contact." + I(i), ToHex(f));
        Emit("out.contact." + I(i), DumpContact(ParseContactFrame(f)));
    }
    // Truncated / wrong-code frames.
    for (int i = 0; i < 6; i++) {
        ByteBuffer f = rng.Bytes(90 + i * 5);
        if (!f.empty())
            f[0] = RESP_CODE_CONTACT;
        Emit("in.contact_trunc." + I(i), ToHex(f));
        Emit("out.contact_trunc." + I(i), DumpContact(ParseContactFrame(f)));
    }

    for (int i = 0; i < 8; i++) {
        ByteWriter w;
        w.U8(RESP_CODE_SELF_INFO);
        w.U8(static_cast<uint8_t>(rng.Below(5)));
        w.U8(rng.Byte());
        w.U8(rng.Byte());
        w.Bytes(rng.Bytes(32));
        w.I32(static_cast<int32_t>(rng.Next() % 90000000) - 45000000);
        w.I32(static_cast<int32_t>(rng.Next() % 90000000) - 45000000);
        w.U8(rng.Byte());
        w.U8(rng.Byte());
        w.U8(rng.Byte());
        w.U8(rng.Byte());
        w.U32(869525000u);
        w.U32(250000u);
        w.U8(11);
        w.U8(5);
        w.Str("selfnode" + I(i));
        ByteBuffer f = w.Take();
        Emit("in.selfinfo." + I(i), ToHex(f));
        Emit("out.selfinfo." + I(i), DumpSelfInfo(ParseSelfInfo(f)));
    }
    for (int n = 50; n < 60; n++) {
        ByteBuffer f = rng.Bytes(n);
        f[0] = RESP_CODE_SELF_INFO;
        Emit("in.selfinfo_len." + I(n), ToHex(f));
        Emit("out.selfinfo_len." + I(n), DumpSelfInfo(ParseSelfInfo(f)));
    }

    const int fws[] = { 1, 2, 3, 8, 9, 10, 14 };
    for (size_t i = 0; i < sizeof(fws) / sizeof(fws[0]); i++) {
        ByteWriter w;
        w.U8(RESP_CODE_DEVICE_INFO);
        w.U8(static_cast<uint8_t>(fws[i]));
        w.U8(50);
        w.U8(8);
        w.U32(123456);
        w.StrTruncated("build" + I(i), 12, 12);
        w.StrTruncated("Heltec V3", 40, 40);
        w.StrTruncated("1.8.0", 20, 20);
        w.U8(1);
        w.U8(2);
        ByteBuffer f = w.Take();
        Emit("in.devinfo." + I(fws[i]), ToHex(f));
        Emit("out.devinfo." + I(fws[i]), DumpDeviceInfo(ParseDeviceInfo(f)));
    }

    {
        ByteBuffer f;
        f.push_back(RESP_CODE_BATT_AND_STORAGE);
        f.push_back(0x10);
        f.push_back(0x27);
        Emit("in.batt.short", ToHex(f));
        BattAndStorage b = ParseBattAndStorage(f);
        Emit("out.batt.short",
            b.valid ? I(b.level) + "/" + (b.has_storage ? I(b.used_kb) + "," + I(b.total_kb)
                                                        : std::string("-"))
                    : std::string("invalid"));
        ByteWriter w;
        w.U8(RESP_CODE_BATT_AND_STORAGE);
        w.U16(4100);
        w.U32(1024);
        w.U32(8192);
        ByteBuffer f2 = w.Take();
        Emit("in.batt.full", ToHex(f2));
        BattAndStorage b2 = ParseBattAndStorage(f2);
        Emit("out.batt.full",
            b2.valid ? I(b2.level) + "/" + (b2.has_storage ? I(b2.used_kb) + "," + I(b2.total_kb)
                                                           : std::string("-"))
                     : std::string("invalid"));
    }

    // Contact messages: v1/v3, plain/CLI/signed.
    const int codes[] = { RESP_CODE_CONTACT_MSG_RECV, RESP_CODE_CONTACT_MSG_RECV_V3 };
    const int txts[] = { TXT_TYPE_PLAIN, TXT_TYPE_CLI_DATA, TXT_TYPE_SIGNED, TXT_TYPE_CLI_COMMAND };
    for (size_t ci = 0; ci < 2; ci++) {
        for (size_t ti = 0; ti < 4; ti++) {
            ByteWriter w;
            w.U8(static_cast<uint8_t>(codes[ci]));
            if (codes[ci] == RESP_CODE_CONTACT_MSG_RECV_V3) {
                w.U8(static_cast<uint8_t>(rng.Byte()));
                w.U16(0);
            }
            w.Bytes(rng.Bytes(6));
            w.U8(rng.Byte());
            w.U8(static_cast<uint8_t>(txts[ti]));
            w.U32(1700000000u);
            if (txts[ti] == TXT_TYPE_SIGNED)
                w.Bytes(rng.Bytes(4));
            w.StrNul("message body " + I(ti));
            ByteBuffer f = w.Take();
            std::string id = I(static_cast<long long>(ci)) + "." + I(static_cast<long long>(ti));
            Emit("in.cmsg." + id, ToHex(f));
            Emit("out.cmsg." + id, DumpContactMsg(ParseContactMessage(f)));
        }
    }
    for (int n = 10; n < 18; n++) {
        ByteBuffer f = rng.Bytes(n);
        f[0] = RESP_CODE_CONTACT_MSG_RECV_V3;
        Emit("in.cmsg_len." + I(n), ToHex(f));
        Emit("out.cmsg_len." + I(n), DumpContactMsg(ParseContactMessage(f)));
    }

    for (size_t ci = 0; ci < 2; ci++) {
        int code = ci ? RESP_CODE_CHANNEL_MSG_RECV_V3 : RESP_CODE_CHANNEL_MSG_RECV;
        const char* bodies[] = { "alice: hello there", "no separator here", "bob: s:AQID",
            "a: b: c" };
        for (size_t bi = 0; bi < 4; bi++) {
            ByteWriter w;
            w.U8(static_cast<uint8_t>(code));
            if (code == RESP_CODE_CHANNEL_MSG_RECV_V3) {
                w.U8(rng.Byte());
                w.U16(0);
            }
            w.U8(static_cast<uint8_t>(bi));
            w.U8(rng.Byte());
            w.U8(TXT_TYPE_PLAIN);
            w.U32(1700000123u);
            w.StrNul(bodies[bi]);
            ByteBuffer f = w.Take();
            std::string id = I(static_cast<long long>(ci)) + "." + I(static_cast<long long>(bi));
            Emit("in.chmsg." + id, ToHex(f));
            Emit("out.chmsg." + id, DumpChannelMsg(ParseChannelMessage(f)));
        }
    }

    {
        ByteWriter w;
        w.U8(RESP_CODE_CHANNEL_DATA_RECV);
        w.U8(0xF0);
        w.U16(0);
        w.U8(2);
        w.U8(0x41);
        w.U16(0x1234);
        w.U8(5);
        w.Bytes(rng.Bytes(5));
        ByteBuffer f = w.Take();
        Emit("in.chdata", ToHex(f));
        ChannelData d = ParseChannelData(f);
        Emit("out.chdata",
            d.valid ? "snr=" + F(d.snr) + ";idx=" + I(d.channel_idx) + ";plen=" + I(d.path_len)
                    + ";hs=" + I(d.path_hash_size) + ";dt=" + I(d.data_type) + ";dl="
                    + I(d.data_len) + ";pl=" + d.payload
                    : std::string("invalid"));
    }

    {
        ByteWriter w;
        w.U8(RESP_CODE_CHANNEL_INFO);
        w.U8(1);
        w.StrTruncated("general", 32, 32);
        w.Bytes(rng.Bytes(16));
        ByteBuffer f = w.Take();
        Emit("in.chinfo", ToHex(f));
        ChannelInfo c = ParseChannelInfo(f);
        Emit("out.chinfo",
            c.valid ? I(c.channel_idx) + "/" + Q(c.channel_name) + "/" + ToHex(c.channel_secret)
                    : std::string("invalid"));
    }

    {
        ByteWriter w;
        w.U8(RESP_CODE_SENT);
        w.U8(2);
        w.Bytes(rng.Bytes(4));
        w.U32(5000);
        ByteBuffer f = w.Take();
        Emit("in.sent", ToHex(f));
        SentResponse s = ParseSentResponse(f);
        Emit("out.sent",
            s.valid ? I(s.type) + "/" + ToHex(s.expected_ack) + "/" + I(s.suggested_timeout)
                    : std::string("invalid"));
    }

    for (int i = 0; i < 3; i++) {
        ByteWriter w;
        w.U8(PUSH_CODE_SEND_CONFIRMED);
        w.Bytes(rng.Bytes(4));
        if (i > 0)
            w.U32(1234 + i);
        ByteBuffer f = w.Take();
        Emit("in.ack." + I(i), ToHex(f));
        SendConfirmed s = ParsePushSendConfirmed(f);
        Emit("out.ack." + I(i),
            s.valid ? s.code + "/" + (s.has_trip_time ? I(s.trip_time_ms) : std::string("-"))
                    : std::string("invalid"));
    }

    for (int i = 0; i < 4; i++) {
        ByteBuffer f;
        f.push_back(RESP_CODE_AUTO_ADD_CONFIG);
        f.push_back(static_cast<uint8_t>(i * 7));
        if (i % 2)
            f.push_back(static_cast<uint8_t>(i));
        Emit("in.autoadd." + I(i), ToHex(f));
        AutoAddConfig a = ParseAutoAddConfig(f);
        Emit("out.autoadd." + I(i),
            a.valid ? I(a.config) + "/" + I(a.max_hops) : std::string("invalid"));
    }

    {
        ByteBuffer f1;
        f1.push_back(RESP_CODE_DEFAULT_FLOOD_SCOPE);
        Emit("in.dscope.none", ToHex(f1));
        DefaultFloodScope s1 = ParseDefaultFloodScope(f1);
        Emit("out.dscope.none",
            s1.valid ? (s1.has_scope ? Q(s1.scope_name) + "/" + s1.scope_key : std::string("empty"))
                     : std::string("invalid"));
        ByteWriter w;
        w.U8(RESP_CODE_DEFAULT_FLOOD_SCOPE);
        w.StrTruncated("#eu-region", 31, 31);
        w.Bytes(rng.Bytes(16));
        ByteBuffer f2 = w.Take();
        Emit("in.dscope.set", ToHex(f2));
        DefaultFloodScope s2 = ParseDefaultFloodScope(f2);
        Emit("out.dscope.set",
            s2.valid ? (s2.has_scope ? Q(s2.scope_name) + "/" + s2.scope_key : std::string("empty"))
                     : std::string("invalid"));
    }

    for (int i = 0; i < 9; i++) {
        ByteBuffer f;
        f.push_back(RESP_CODE_ERR);
        if (i < 8)
            f.push_back(static_cast<uint8_t>(i));
        ErrorResponse e = ParseErrorResponse(f);
        Emit("out.err." + I(i), I(e.code) + "/" + Q(e.message));
    }

    for (int i = 0; i < 6; i++) {
        ByteWriter w;
        w.U8(RESP_CODE_ADVERT_PATH);
        w.U32(1700000000u + i);
        w.U8(i == 5 ? 0xFF : static_cast<uint8_t>(EncodePathLen(i, (i % 3) + 1)));
        w.Bytes(rng.Bytes(static_cast<size_t>(i) * ((i % 3) + 1)));
        ByteBuffer f = w.Take();
        Emit("in.advpath." + I(i), ToHex(f));
        AdvertPathResponse p = ParseAdvertPathResponse(f);
        std::string hops;
        for (size_t h = 0; h < p.hops.size(); h++) {
            if (h) hops += ",";
            hops += p.hops[h];
        }
        Emit("out.advpath." + I(i),
            p.valid ? I(p.path_length) + "/" + I(p.path_hash_size) + "/" + ToHex(p.path) + "/"
                    + I(p.timestamp) + "/" + hops
                    : std::string("invalid"));
    }

    for (int flags = 0; flags < 4; flags++) {
        int hs = 1 << flags;
        int hops = 3;
        ByteWriter w;
        w.U8(PUSH_CODE_TRACE_DATA);
        w.U8(0);
        w.U8(static_cast<uint8_t>(hops * hs));
        w.U8(static_cast<uint8_t>(flags));
        w.Bytes(rng.Bytes(4));  // tag
        w.U32(0);               // auth
        w.Bytes(rng.Bytes(static_cast<size_t>(hops * hs)));
        w.Bytes(rng.Bytes(static_cast<size_t>(hops)));
        ByteBuffer f = w.Take();
        Emit("in.trace." + I(flags), ToHex(f));
        TraceResponse t = ParseTraceResponse(f);
        std::string out = t.valid ? "tag=" + ToHex(t.tag) + ";hs=" + I(t.path_hash_size) + ";hops="
                                  : std::string("invalid");
        if (t.valid) {
            for (size_t h = 0; h < t.path_hops.size(); h++) {
                if (h) out += ",";
                out += ToHex(t.path_hops[h]);
            }
            out += ";snr=";
            for (size_t s = 0; s < t.snr_values.size(); s++) {
                if (s) out += ",";
                out += F(t.snr_values[s]);
            }
        }
        Emit("out.trace." + I(flags), out);
    }

    for (int extra = 0; extra < 3; extra++) {
        ByteWriter w;
        w.U8(PUSH_CODE_STATUS_RESPONSE);
        w.U8(0);
        w.Bytes(rng.Bytes(6));
        w.U16(4100);
        w.U16(3);
        w.I16(-120);
        w.I16(-95);
        w.U32(1000);
        w.U32(2000);
        w.U32(3000);
        w.U32(4000);
        w.U32(5);
        w.U32(6);
        w.U32(7);
        w.U32(8);
        w.U16(9);
        w.I16(-20);
        if (extra >= 1) {
            w.U16(11);
            w.U16(12);
        }
        if (extra >= 2) {
            w.U32(13);
            w.U32(14);
        }
        ByteBuffer f = w.Take();
        Emit("in.status." + I(extra), ToHex(f));
        Emit("out.status." + I(extra), DumpStatus(ParseStatusResponse(f)));
    }

    for (int kl = 2; kl <= 8; kl += 2) {
        ByteWriter w;
        w.U8(RESP_CODE_BINARY_RESPONSE);
        w.U8(0);
        w.Bytes(rng.Bytes(4));
        w.U16(25);
        w.U16(3);
        for (int i = 0; i < 3; i++) {
            w.Bytes(rng.Bytes(static_cast<size_t>(kl)));
            w.U32(1700000000u + i);
            w.U8(rng.Byte());
        }
        ByteBuffer f = w.Take();
        Emit("in.neigh." + I(kl), ToHex(f));
        NeighborsResponse n = ParseNeighborsResponse(f, static_cast<uint8_t>(kl));
        std::string out = "total=" + I(n.total) + ";";
        for (size_t i = 0; i < n.neighbors.size(); i++)
            out += n.neighbors[i].pub_key_hex + "/" + I(n.neighbors[i].last_heard) + "/"
                + F(n.neighbors[i].snr) + ",";
        Emit("out.neigh." + I(kl), out);
    }

    for (int i = 0; i < 4; i++) {
        ByteWriter w;
        w.U8(RESP_CODE_CONTROL_DATA);
        w.Bytes(rng.Bytes(2));
        w.U8(static_cast<uint8_t>(EncodePathLen(i, 1)));
        w.Bytes(rng.Bytes(static_cast<size_t>(i)));
        w.U8(static_cast<uint8_t>(0x90 | (i % 5)));  // sub_type 9 + node type
        w.U8(rng.Byte());
        w.U32(0xAABBCCDDu);
        w.Bytes(rng.Bytes(8));
        ByteBuffer f = w.Take();
        Emit("in.disc." + I(i), ToHex(f));
        DiscoverResponse d = ParseDiscoverResponse(f);
        Emit("out.disc." + I(i),
            d.valid ? I(d.node_type) + "/" + F(d.snr) + "/" + I(d.tag) + "/" + d.pub_key_hex
                    : std::string("invalid"));
    }

    for (int sub = 0; sub < 3; sub++) {
        ByteWriter w;
        w.U8(RESP_CODE_STATS);
        w.U8(static_cast<uint8_t>(sub));
        w.Bytes(rng.Bytes(32));
        ByteBuffer f = w.Take();
        Emit("in.stats." + I(sub), ToHex(f));
        Emit("out.stats." + I(sub), DumpStats(ParseStatsResponse(f)));
    }

    {
        ByteWriter w;
        w.U8(RESP_CODE_ALLOWED_REPEAT_FREQ);
        w.U32(863000);
        w.U32(870000);
        w.U32(902000);
        w.U32(928000);
        w.U32(0);
        w.U32(0);
        w.U32(111);
        w.U32(222);
        ByteBuffer f = w.Take();
        Emit("in.freq", ToHex(f));
        std::vector<FreqRange> r = ParseAllowedRepeatFreq(f);
        std::string out;
        for (size_t i = 0; i < r.size(); i++)
            out += I(r[i].lower_khz) + "-" + I(r[i].upper_khz) + ",";
        Emit("out.freq", out);
    }

    {
        const char* cvs[] = { "gps:1,bat:80", "", "novalue", "a:1,b:2,c:3", "k:v:extra" };
        for (size_t i = 0; i < sizeof(cvs) / sizeof(cvs[0]); i++) {
            ByteWriter w;
            w.U8(RESP_CODE_CUSTOM_VARS);
            w.Str(cvs[i]);
            ByteBuffer f = w.Take();
            std::vector<std::pair<std::string, std::string> > v = ParseCustomVars(f);
            std::string out;
            for (size_t j = 0; j < v.size(); j++)
                out += Q(v[j].first) + "=" + Q(v[j].second) + ",";
            Emit("out.cvars." + I(static_cast<long long>(i)), out);
        }
    }

    {
        ByteWriter w;
        w.U8(RESP_CODE_CURR_TIME);
        w.U32(1700001234u);
        ByteBuffer f = w.Take();
        uint32_t t = 0;
        Emit("out.currtime", ParseCurrTime(f, &t) ? I(t) : std::string("invalid"));
        ByteBuffer bad(3, 9);
        bad[0] = RESP_CODE_CURR_TIME;
        Emit("out.currtime.short", ParseCurrTime(bad, &t) ? I(t) : std::string("invalid"));
    }

    {
        ByteWriter w;
        w.U8(RESP_CODE_CLI_REPLY);
        w.Str("freq: 869.525");
        w.U8(0);
        w.U8(0);
        ByteBuffer f = w.Take();
        std::string txt;
        Emit("out.clireply", ParseCliReply(f, &txt) ? Q(txt) : std::string("invalid"));
    }

    // Advert frames and raw log packets.
    for (int i = 0; i < 10; i++) {
        ByteWriter w;
        w.U8(static_cast<uint8_t>(rng.Byte()));
        w.U8(static_cast<uint8_t>(EncodePathLen(i % 4, (i % 3) + 1)));
        w.Bytes(rng.Bytes(static_cast<size_t>(i % 4) * ((i % 3) + 1)));
        w.Bytes(rng.Bytes(32));
        w.U32(1700000000u);
        w.Bytes(rng.Bytes(64));
        uint8_t flags = static_cast<uint8_t>((i % 2 ? 0x80 : 0) | (i % 3 ? 0x10 : 0) | (i % 4));
        w.U8(flags);
        if (flags & 0x10) {
            w.I32(50123456);
            w.I32(14456789);
        }
        if (flags & 0x80)
            w.StrNul("advname" + I(i));
        ByteBuffer f = w.Take();
        Emit("in.advframe." + I(i), ToHex(f));
        AdvertFrameInfo a = ParseAdvertFrame(f);
        Emit("out.advframe." + I(i),
            a.valid ? a.pub_key_hex + "/" + Q(a.name) + "/" + I(a.type) : std::string("invalid"));
    }

    // Random raw packets exercise the LOG_DATA decoder's bounds handling.
    for (int i = 0; i < 24; i++) {
        ByteBuffer raw = rng.Bytes(1 + rng.Below(140));
        Emit("in.logpkt." + I(i), ToHex(raw));
        Emit("out.logpkt." + I(i), DumpLogPacket(DecodeLogPacket(raw)));
    }
    // Structured GroupText and advert packets.
    for (int rt = 0; rt < 4; rt++) {
        ByteWriter w;
        w.U8(static_cast<uint8_t>((PAYLOAD_GROUP_TEXT << 2) | rt));
        if (rt == 0 || rt == 3)
            w.Bytes(rng.Bytes(4));
        w.U8(static_cast<uint8_t>(EncodePathLen(2, 1)));
        w.Bytes(rng.Bytes(2));
        w.U8(0xAB);              // channel hash
        w.Bytes(rng.Bytes(18));  // mac + ciphertext
        ByteBuffer f = w.Take();
        Emit("in.grouptext." + I(rt), ToHex(f));
        Emit("out.grouptext." + I(rt), DumpLogPacket(DecodeLogPacket(f)));
    }
    {
        ByteWriter w;
        w.U8(static_cast<uint8_t>((PAYLOAD_ADVERT << 2) | 1));
        w.U8(static_cast<uint8_t>(EncodePathLen(1, 1)));
        w.Bytes(rng.Bytes(1));
        w.Bytes(rng.Bytes(32));
        w.U32(1700000000u);
        w.Bytes(rng.Bytes(64));
        w.U8(0x91);  // name + location flags, type 1
        w.I32(50123456);
        w.I32(14456789);
        w.StrNul("repeater-x");
        ByteBuffer f = w.Take();
        Emit("in.advpkt", ToHex(f));
        Emit("out.advpkt", DumpLogPacket(DecodeLogPacket(f)));
    }
}

static void TestLpp()
{
    // Cayenne LPP telemetry.
    {
        Lcg lrng(4242);
        const uint8_t types[] = { 0, 1, 2, 3, 100, 101, 102, 103, 104, 113, 115, 116, 117, 118,
            120, 121, 125, 128, 130, 131, 132, 133, 134, 135, 136, 142, 200 };
        for (size_t t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
            ByteWriter w;
            w.U8(static_cast<uint8_t>(t + 1));
            w.U8(types[t]);
            w.Bytes(lrng.Bytes(12));
            ByteBuffer f = w.Take();
            std::vector<LppReading> v = ParseCayenneLpp(f);
            std::string out;
            for (size_t i = 0; i < v.size(); i++) {
                char buf[256];
                if (v[i].is_vector)
                    snprintf(buf, sizeof(buf), "%d/%d/%s/%s/%.6f,%.6f,%.6f;", v[i].channel,
                        v[i].type, v[i].name, v[i].unit, v[i].x, v[i].y, v[i].z);
                else
                    snprintf(buf, sizeof(buf), "%d/%d/%s/%s/%.6f;", v[i].channel, v[i].type,
                        v[i].name, v[i].unit, v[i].value);
                out += buf;
            }
            Emit("in.lpp." + I(static_cast<long long>(t)), ToHex(f));
            Emit("out.lpp." + I(static_cast<long long>(t)), out);
        }
        // Multi-reading payloads and random noise.
        for (int i = 0; i < 10; i++) {
            ByteBuffer f = lrng.Bytes(2 + lrng.Below(24));
            std::vector<LppReading> v = ParseCayenneLpp(f);
            std::string out;
            for (size_t j = 0; j < v.size(); j++) {
                char buf[256];
                if (v[j].is_vector)
                    snprintf(buf, sizeof(buf), "%d/%d/%.6f,%.6f,%.6f;", v[j].channel, v[j].type,
                        v[j].x, v[j].y, v[j].z);
                else
                    snprintf(buf, sizeof(buf), "%d/%d/%.6f;", v[j].channel, v[j].type,
                        v[j].value);
                out += buf;
            }
            Emit("in.lpprand." + I(i), ToHex(f));
            Emit("out.lpprand." + I(i), out);
        }
    }
}

int main()
{
    TestBuilders();
    TestHelpers();
    TestParsers();
    TestLpp();
    return 0;
}
