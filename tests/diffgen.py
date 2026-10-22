#!/usr/bin/env python3
# Copyright 2026, the Meshy for Haiku contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Reference half of the differential test: emits the same "name|value" lines as
# tests/diffgen.cpp, but computed with the upstream Meshy Python modules.
# Run tests/run_diff.sh to build both and compare.

import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_UPSTREAM = os.environ.get("MESHY_UPSTREAM")
if not _UPSTREAM:
    sys.stderr.write("set MESHY_UPSTREAM to the upstream meshy checkout\n")
    sys.exit(2)

# Upstream's package is the `src` directory; expose it as `meshy`.
import importlib.util
import types

_pkg = types.ModuleType("meshy")
_pkg.__path__ = [os.path.join(_UPSTREAM, "src")]
sys.modules["meshy"] = _pkg

from meshy import mesh_crypto, protocol  # noqa: E402
from meshy.smaz import try_decode_prefixed  # noqa: E402

P = protocol


# ─── Deterministic PRNG, mirrored from diffgen.cpp ────────────────


class Lcg:
    def __init__(self, seed):
        self.state = seed

    def next(self):
        self.state = (self.state * 1103515245 + 12345) & 0x7FFFFFFF
        return self.state

    def byte(self):
        return (self.next() >> 16) & 0xFF

    def below(self, n):
        return self.next() % n if n else 0

    def bytes(self, n):
        return bytes(self.byte() for _ in range(n))


class W:
    """Mirror of the C++ ByteWriter, for building test fixtures."""

    def __init__(self):
        self.b = bytearray()

    def u8(self, v):
        self.b.append(v & 0xFF)
        return self

    def u16(self, v):
        self.b += struct.pack("<H", v & 0xFFFF)
        return self

    def u32(self, v):
        self.b += struct.pack("<I", v & 0xFFFFFFFF)
        return self

    def i16(self, v):
        self.b += struct.pack("<h", v)
        return self

    def i32(self, v):
        self.b += struct.pack("<i", v)
        return self

    def raw(self, data):
        self.b += data
        return self

    def zeros(self, n):
        self.b += bytes(n)
        return self

    def s(self, text):
        self.b += text.encode("utf-8")
        return self

    def snul(self, text):
        self.b += text.encode("utf-8") + b"\x00"
        return self

    def strunc(self, text, max_bytes, pad_to=0):
        t = P._truncate_utf8(text, max_bytes)
        self.b += t
        if pad_to > len(t):
            self.b += bytes(pad_to - len(t))
        return self

    def take(self):
        return bytes(self.b)


OUT = []


def emit(name, value):
    OUT.append("%s|%s" % (name, value))


def emit_frame(name, frame):
    emit(name, frame.hex())


def F(v):
    return "%.6f" % v


def I(v):
    return str(int(v))


def B(v):
    return "1" if v else "0"


def Q(s):
    """Escape exactly as the C++ Q(): per UTF-8 byte."""
    if isinstance(s, str):
        data = s.encode("utf-8", "surrogatepass")
    else:
        data = s
    out = []
    for c in data:
        if c < 0x20 or c in (0x7C, 0x5C) or c >= 0x7F:
            out.append("\\x%02x" % c)
        else:
            out.append(chr(c))
    return "".join(out)


# ─── Builders ────────────────────────────────────────────────────


def test_builders():
    TS = 1760000000
    rng = Lcg(12345)

    emit_frame("app_start.default", P.build_app_start("Meshy", P.APP_PROTOCOL_VERSION))
    emit_frame("app_start.named", P.build_app_start("HaikuMeshy", 7))
    emit_frame("app_start.utf8", P.build_app_start("Méšy", 4))
    emit_frame("device_query.default", P.build_device_query())
    emit_frame("device_query.v9", P.build_device_query(9))
    emit_frame("get_contacts", P.build_get_contacts())
    emit_frame("get_contacts.since", P.build_get_contacts(1700000123))
    emit_frame("get_device_time", P.build_get_device_time())
    emit_frame("set_device_time", P.build_set_device_time(TS))
    emit_frame("get_batt", P.build_get_batt_and_storage())
    emit_frame("sync_next", P.build_sync_next_message())

    key32 = rng.bytes(32)
    emit("fixture.key32", key32.hex())

    emit_frame("send_text.basic", P.build_send_text_msg(key32, "hello world", 0, TS))
    emit_frame("send_text.attempt", P.build_send_text_msg(key32, "retry", 3, TS + 5))
    emit_frame("send_text.attempt_clamp", P.build_send_text_msg(key32, "clamp", 255, TS))
    emit_frame("send_text.utf8", P.build_send_text_msg(key32, "příliš žlu", 0, TS))
    emit_frame("send_text.empty", P.build_send_text_msg(key32, "", 0, TS))

    emit_frame("send_chan.basic", P.build_send_channel_text_msg(0, "chan msg", TS))
    emit_frame("send_chan.idx7", P.build_send_channel_text_msg(7, "another", TS))

    emit("cli_txt_type.chat_cli", I(P.cli_txt_type(P.ADV_TYPE_CHAT, True)))
    emit("cli_txt_type.chat_nocli", I(P.cli_txt_type(P.ADV_TYPE_CHAT, False)))
    emit("cli_txt_type.repeater", I(P.cli_txt_type(P.ADV_TYPE_REPEATER, True)))
    emit_frame("send_cli.data", P.build_send_cli_command(key32, "ver", 0, TS, P.TXT_TYPE_CLI_DATA))
    emit_frame(
        "send_cli.cmd", P.build_send_cli_command(key32, "reboot", 1, TS, P.TXT_TYPE_CLI_COMMAND)
    )
    emit_frame("run_cli", P.build_run_cli_command("get freq"))

    emit_frame("self_advert.zero", P.build_send_self_advert(False))
    emit_frame("self_advert.flood", P.build_send_self_advert(True))
    emit_frame("set_advert_name.short", P.build_set_advert_name("node"))
    emit_frame("set_advert_name.long", P.build_set_advert_name("abcdefghijklmnopqrstuvwxyz0123456789"))
    emit_frame("set_advert_name.utf8trunc", P.build_set_advert_name("žluťoučký kůň úpěl ďábelské ódy"))
    emit_frame("set_latlon.pos", P.build_set_advert_latlon(50.0755, 14.4378))
    emit_frame("set_latlon.neg", P.build_set_advert_latlon(-33.8688, -151.2093))
    emit_frame("set_latlon.zero", P.build_set_advert_latlon(0.0, 0.0))

    emit_frame("remove_contact", P.build_remove_contact(key32))
    emit_frame("get_contact_by_key", P.build_get_contact_by_key(key32))
    emit_frame("reset_path", P.build_reset_path(key32))
    emit_frame("share_contact", P.build_share_contact(key32))
    emit_frame("export_contact", P.build_export_contact(key32))
    emit_frame("import_contact", P.build_import_contact(rng.bytes(40)))

    path3 = rng.bytes(3)
    emit("fixture.path3", path3.hex())
    emit_frame(
        "add_contact.minimal",
        P.build_add_update_contact(key32, P.ADV_TYPE_CHAT, 0, 0, b"", "bob", last_advert=TS),
    )
    emit_frame(
        "add_contact.path",
        P.build_add_update_contact(key32, P.ADV_TYPE_REPEATER, 2, 3, path3, "rep1", last_advert=TS),
    )
    emit_frame(
        "add_contact.loc",
        P.build_add_update_contact(
            key32, P.ADV_TYPE_ROOM, 0, 0, b"", "room", lat=50.1, lon=14.2, last_advert=TS
        ),
    )
    emit_frame(
        "add_contact.loc_lastmod",
        P.build_add_update_contact(
            key32,
            P.ADV_TYPE_SENSOR,
            1,
            3,
            path3,
            "sens",
            lat=-12.5,
            lon=130.75,
            last_modified=TS + 99,
            last_advert=TS,
        ),
    )
    emit_frame(
        "add_contact.lastmod_only",
        P.build_add_update_contact(
            key32, P.ADV_TYPE_CHAT, 0, 0, b"", "x", last_modified=TS + 7, last_advert=TS
        ),
    )

    emit_frame("get_channel", P.build_get_channel(3))
    emit_frame("set_channel.basic", P.build_set_channel(0, "general", rng.bytes(16)))
    emit_frame("set_channel.shortpsk", P.build_set_channel(1, "short", rng.bytes(4)))
    emit_frame(
        "set_channel.longname",
        P.build_set_channel(2, "a-very-long-channel-name-that-overflows", rng.bytes(16)),
    )

    emit_frame("radio_params", P.build_set_radio_params(869525, 250, 11, 5))
    emit_frame("radio_params.repeat_on", P.build_set_radio_params(915000, 125, 7, 8, True))
    emit_frame("radio_params.repeat_off", P.build_set_radio_params(915000, 125, 7, 8, False))
    emit_frame("tx_power.pos", P.build_set_radio_tx_power(22))
    emit_frame("tx_power.neg", P.build_set_radio_tx_power(-5))
    emit_frame("reboot", P.build_reboot())
    emit_frame("factory_reset", P.build_factory_reset())
    emit_frame("path_hash_mode", P.build_set_path_hash_mode(2))
    emit_frame("device_pin", P.build_set_device_pin(123456))
    emit_frame("other_params", P.build_set_other_params(1, 0x15, 2, 3))

    emit_frame("get_custom_vars", P.build_get_custom_vars())
    emit_frame("set_custom_var", P.build_set_custom_var("gps:1"))

    emit_frame("flood_scope.empty", P.build_set_flood_scope("", False))
    emit_frame("flood_scope.star", P.build_set_flood_scope("*", False))
    emit_frame("flood_scope.named", P.build_set_flood_scope("czechia", False))
    emit_frame("flood_scope.hash", P.build_set_flood_scope("#czechia", False))
    emit_frame("flood_scope.unscoped", P.build_set_flood_scope("#eu", True))
    emit_frame("default_scope.clear", P.build_set_default_flood_scope(""))
    emit_frame("default_scope.named", P.build_set_default_flood_scope("#eu-region"))
    emit_frame("default_scope.nohash", P.build_set_default_flood_scope("eu-region"))
    emit_frame("default_scope.get", P.build_get_default_flood_scope())

    emit_frame("get_advert_path", P.build_get_advert_path(key32))
    emit_frame("path_discovery", P.build_path_discovery(key32))
    emit_frame("trace_path", P.build_trace_path(0xDEADBEEF, rng.bytes(4), 1))

    emit_frame("send_login", P.build_send_login(key32, "hunter2"))
    emit_frame("logout", P.build_logout(key32))
    emit_frame("status_req", P.build_send_status_req(key32))

    emit_frame("get_autoadd", P.build_get_auto_add_config())
    emit_frame("set_autoadd.all", P.build_set_auto_add_config(True, True, True, True, True, 5))
    emit_frame("set_autoadd.chat", P.build_set_auto_add_config(True, False, False, False, False, 0))
    emit_frame(
        "set_autoadd.clamp", P.build_set_auto_add_config(False, True, False, False, False, 200)
    )

    emit_frame("discover_req", P.build_send_discover_req(0xFF, True, 0x11223344))
    emit_frame("discover_req.full", P.build_send_discover_req(0x06, False, 0x55667788))
    emit_frame("get_neighbors", P.build_get_neighbors(key32, 10, 0, 4))
    emit_frame("get_neighbors.off", P.build_get_neighbors(key32, 5, 300, 6))
    emit_frame("get_acl", P.build_get_acl(key32))
    emit_frame("get_telemetry_chat.self", P.build_get_telemetry_chat())
    emit_frame("get_telemetry_chat.peer", P.build_get_telemetry_chat(key32))

    path6 = rng.bytes(6)
    emit("fixture.path6", path6.hex())
    emit_frame("anon_regions", P.build_anon_req_regions(key32, 3, path6, 2))
    emit_frame("anon_owner", P.build_anon_req_owner(key32, 6, path6, 1))
    emit_frame("anon_regions.clamp", P.build_anon_req_regions(key32, 99, path6, 1))

    emit_frame("allowed_freq", P.build_get_allowed_repeat_freq())
    emit_frame("stats.core", P.build_get_stats(P.STATS_TYPE_CORE))
    emit_frame("stats.radio", P.build_get_stats(P.STATS_TYPE_RADIO))
    emit_frame("export_privkey", P.build_export_private_key())
    emit_frame("import_privkey", P.build_import_private_key(rng.bytes(64)))
    emit_frame("import_privkey.long", P.build_import_private_key(rng.bytes(80)))

    emit("max_dm_text", I(P.max_dm_text_bytes()))
    emit("max_chan_text.empty", I(P.max_channel_text_bytes("")))
    emit("max_chan_text.name", I(P.max_channel_text_bytes("alice")))
    emit("max_chan_text.long", I(P.max_channel_text_bytes("x" * 100)))


# ─── Helpers ─────────────────────────────────────────────────────


def test_helpers():
    for plen in range(256):
        hc, hs = P.decode_path_len_byte(plen)
        phc, phs = P._parse_path_length_byte(plen)
        emit("pathlen.%d" % plen, "%d,%d,%d,%d" % (hc, hs, phc, phs))
    for hc in range(-2, 70):
        for hs in range(5):
            emit("encpath.%d.%d" % (hc, hs), I(P.encode_path_len(hc, hs)))
    for i in range(-128, 128):
        emit("snr.%d" % i, F(P._parse_snr(struct.pack("b", i))))

    scopes = ["", "0", "None", "*", "eu", "#eu", "#Czechia", "$private", " spaced ", "éregion"]
    for i, s in enumerate(scopes):
        emit("scopekey.%d" % i, P.compute_scope_key(s).hex())
        dk = mesh_crypto.derive_scope_key(s)
        emit("derivescope.%d" % i, dk.hex() if dk is not None else "none")
    chans = ["#test", "#TEST", "general", "", "#čeština"]
    for i, c in enumerate(chans):
        emit("hashtagpsk.%d" % i, P.hashtag_channel_psk(c).hex())

    rng = Lcg(777)
    for i in range(8):
        psk = rng.bytes(16)
        emit("chanhash.%d" % i, psk.hex() + "=" + mesh_crypto.channel_hash(psk.hex()))
    regions = ["eu", "#eu", "us", "$priv"]
    for i in range(6):
        payload = rng.bytes(1 + rng.below(40))
        ptype = rng.below(16)
        k = mesh_crypto.derive_scope_key("eu")
        code = mesh_crypto.calc_transport_code(k, ptype, payload)
        emit("tcode.%d" % i, "%s/%d=%d" % (payload.hex(), ptype, code))
        emit("region.%d" % i, mesh_crypto.resolve_region_name(code, ptype, payload, regions))

    smaz_cases = [
        "plain text",
        "s:",
        "s:AQID",
        "s:dGhlIHF1aWNr",
        "  s:AQID",
        "s:!!!!",
        "s:-_8A",
        "not s: prefixed",
    ]
    for i, c in enumerate(smaz_cases):
        emit("smaz.%d" % i, Q(try_decode_prefixed(c)))


# ─── Parser dumps ────────────────────────────────────────────────


def dump_contact(c):
    if c is None:
        return "invalid"
    return (
        "pk=%s;type=%d;flags=%d;plen=%d;hs=%d;path=%s;name=%s;adv=%d;lat=%s;lon=%s;lastmod=%s"
        % (
            c["public_key"],
            c["type"],
            c["flags"],
            c["out_path_len"],
            c["path_hash_size"],
            c["out_path"],
            Q(c["adv_name"]),
            c["last_advert"],
            F(c["adv_lat"]),
            F(c["adv_lon"]),
            I(c["lastmod"]) if "lastmod" in c else "-",
        )
    )


def dump_selfinfo(s):
    if s is None:
        return "invalid"
    return (
        "advtype=%d;tx=%d;maxtx=%d;pk=%s;lat=%s;lon=%s;macks=%d;locpol=%d;tenv=%d;tloc=%d;"
        "tbase=%d;manual=%s;freq=%s;bw=%s;sf=%d;cr=%d;name=%s"
        % (
            s["adv_type"],
            s["tx_power"],
            s["max_tx_power"],
            s["public_key"],
            F(s["adv_lat"]),
            F(s["adv_lon"]),
            s["multi_acks"],
            s["adv_loc_policy"],
            s["telemetry_mode_env"],
            s["telemetry_mode_loc"],
            s["telemetry_mode_base"],
            B(s["manual_add_contacts"]),
            F(s["radio_freq"]),
            F(s["radio_bw"]),
            s["radio_sf"],
            s["radio_cr"],
            Q(s["name"]),
        )
    )


def dump_devinfo(d):
    if d is None:
        return "invalid"
    return "fw=%d;maxc=%d;maxch=%d;pin=%d;build=%s;model=%s;ver=%s;repeat=%s;phm=%s" % (
        d["fw_ver"],
        d.get("max_contacts", 0),
        d.get("max_channels", 0),
        d.get("ble_pin", 0),
        Q(d.get("fw_build", "")),
        Q(d.get("model", "")),
        Q(d.get("ver", "")),
        B(d["repeat"]) if "repeat" in d else "-",
        I(d["path_hash_mode"]) if "path_hash_mode" in d else "-",
    )


def dump_cmsg(m):
    if m is None:
        return "invalid"
    return "snr=%s;prefix=%s;plen=%d;hs=%d;txt=%d;ts=%d;sig=%s;text=%s" % (
        F(m["SNR"]) if "SNR" in m else "-",
        m["pubkey_prefix"],
        m["path_len"],
        m["path_hash_size"],
        m["txt_type"],
        m["sender_timestamp"],
        m.get("signature") or "-",
        Q(m["text"]),
    )


def dump_chmsg(m):
    if m is None:
        return "invalid"
    return "snr=%s;idx=%d;plen=%d;hs=%d;txt=%d;ts=%d;text=%s;sender=%s;body=%s" % (
        F(m["SNR"]) if "SNR" in m else "-",
        m["channel_idx"],
        m["path_len"],
        m["path_hash_size"],
        m["txt_type"],
        m["sender_timestamp"],
        Q(m["text"]),
        Q(m["sender_name"]),
        Q(m["msg_text"]),
    )


def dump_status(s):
    if s is None:
        return "invalid"
    return (
        "batt=%d;q=%d;nf=%d;rssi=%d;prx=%d;ptx=%d;tx=%d;up=%d;ftx=%d;dtx=%d;frx=%d;drx=%d;"
        "err=%d;snr=%s;dd=%d;df=%d;rxair=%d;rxerr=%s"
        % (
            s["battery_mv"],
            s["queue_len"],
            s["noise_floor"],
            s["last_rssi"],
            s["packets_recv"],
            s["packets_sent"],
            s["tx_air_secs"],
            s["uptime_secs"],
            s["flood_tx"],
            s["direct_tx"],
            s["flood_rx"],
            s["direct_rx"],
            s["error_events"],
            F(s["last_snr"]),
            s["dup_direct"],
            s["dup_flood"],
            s["rx_air_secs"],
            I(s["recv_errors"]) if s.get("recv_errors") is not None else "-",
        )
    )


def dump_stats(s):
    if s is None:
        return "invalid"
    out = "sub=%d" % s["sub_type"]
    if s["sub_type"] == P.STATS_TYPE_CORE and "battery_mv" in s:
        out += ";batt=%d;up=%d;err=%d;q=%d" % (
            s["battery_mv"],
            s["uptime_secs"],
            s["errors"],
            s["queue_len"],
        )
    elif s["sub_type"] == P.STATS_TYPE_RADIO and "noise_floor" in s:
        out += ";nf=%d;rssi=%d;snr=%s;tx=%d;rx=%d" % (
            s["noise_floor"],
            s["last_rssi"],
            F(s["last_snr"]),
            s["tx_air_secs"],
            s["rx_air_secs"],
        )
    elif s["sub_type"] == P.STATS_TYPE_PACKETS and "recv" in s:
        out += ";recv=%d;sent=%d;ftx=%d;dtx=%d;frx=%d;drx=%d;rxerr=%s" % (
            s["recv"],
            s["sent"],
            s["flood_tx"],
            s["direct_tx"],
            s["flood_rx"],
            s["direct_rx"],
            I(s["recv_errors"]) if "recv_errors" in s else "-",
        )
    return out


def dump_logpkt(p):
    if p is None:
        return "invalid"
    out = "ptype=%d;rtype=%d;hs=%d;tfrom=%s;tto=%s;hash=%s;payload=%s;path=%s" % (
        p["payload_type"],
        p["route_type"],
        p["path_hash_size"],
        p["transport_from"] or "-",
        p["transport_to"] or "-",
        p["pkt_hash"],
        p["payload"].hex(),
        ",".join(p["path"]),
    )
    if "advert" in p:
        a = p["advert"]
        loc = (
            "%s,%s" % (F(a["adv_lat"]), F(a["adv_lon"]))
            if a["adv_lat"] is not None and a["adv_lon"] is not None
            else "-"
        )
        out += ";advert=%s/%d/%s/%d/%s" % (
            a["public_key"],
            a["type"],
            Q(a["adv_name"]),
            a["last_advert"],
            loc,
        )
    if "channel_hash" in p:
        out += ";chan=%s/%s" % (p["channel_hash"], p["ciphertext"].hex())
    return out


# ─── Parsers ─────────────────────────────────────────────────────


def test_parsers():
    rng = Lcg(999)

    for i in range(12):
        w = W()
        w.u8(P.RESP_CODE_CONTACT if i % 2 else P.PUSH_CODE_NEW_ADVERT)
        w.raw(rng.bytes(32))
        w.u8(rng.below(5))
        w.u8(rng.byte())
        w.u8(rng.byte())
        w.raw(rng.bytes(64))
        w.strunc("node-%d" % i, 31, 32)
        w.u32(1700000000 + i)
        w.i32(rng.next() % 90000000 - 45000000)
        w.i32(rng.next() % 90000000 - 45000000)
        if i % 3 == 0:
            w.u32(1700000500 + i)
        f = w.take()
        emit("in.contact.%d" % i, f.hex())
        emit("out.contact.%d" % i, dump_contact(P.parse_contact_frame(f)))
    for i in range(6):
        f = bytearray(rng.bytes(90 + i * 5))
        if f:
            f[0] = P.RESP_CODE_CONTACT
        f = bytes(f)
        emit("in.contact_trunc.%d" % i, f.hex())
        emit("out.contact_trunc.%d" % i, dump_contact(P.parse_contact_frame(f)))

    for i in range(8):
        w = W()
        w.u8(P.RESP_CODE_SELF_INFO)
        w.u8(rng.below(5))
        w.u8(rng.byte())
        w.u8(rng.byte())
        w.raw(rng.bytes(32))
        w.i32(rng.next() % 90000000 - 45000000)
        w.i32(rng.next() % 90000000 - 45000000)
        w.u8(rng.byte())
        w.u8(rng.byte())
        w.u8(rng.byte())
        w.u8(rng.byte())
        w.u32(869525000)
        w.u32(250000)
        w.u8(11)
        w.u8(5)
        w.s("selfnode%d" % i)
        f = w.take()
        emit("in.selfinfo.%d" % i, f.hex())
        emit("out.selfinfo.%d" % i, dump_selfinfo(P.parse_self_info(f)))
    for n in range(50, 60):
        f = bytearray(rng.bytes(n))
        f[0] = P.RESP_CODE_SELF_INFO
        f = bytes(f)
        emit("in.selfinfo_len.%d" % n, f.hex())
        emit("out.selfinfo_len.%d" % n, dump_selfinfo(P.parse_self_info(f)))

    for i, fw in enumerate([1, 2, 3, 8, 9, 10, 14]):
        w = W()
        w.u8(P.RESP_CODE_DEVICE_INFO)
        w.u8(fw)
        w.u8(50)
        w.u8(8)
        w.u32(123456)
        w.strunc("build%d" % i, 12, 12)
        w.strunc("Heltec V3", 40, 40)
        w.strunc("1.8.0", 20, 20)
        w.u8(1)
        w.u8(2)
        f = w.take()
        emit("in.devinfo.%d" % fw, f.hex())
        emit("out.devinfo.%d" % fw, dump_devinfo(P.parse_device_info(f)))

    f = bytes([P.RESP_CODE_BATT_AND_STORAGE, 0x10, 0x27])
    emit("in.batt.short", f.hex())
    b = P.parse_batt_and_storage(f)
    emit(
        "out.batt.short",
        "%d/%s" % (b["level"], "%d,%d" % (b["used_kb"], b["total_kb"]) if "used_kb" in b else "-")
        if b
        else "invalid",
    )
    w = W().u8(P.RESP_CODE_BATT_AND_STORAGE).u16(4100).u32(1024).u32(8192)
    f2 = w.take()
    emit("in.batt.full", f2.hex())
    b2 = P.parse_batt_and_storage(f2)
    emit(
        "out.batt.full",
        "%d/%s"
        % (b2["level"], "%d,%d" % (b2["used_kb"], b2["total_kb"]) if "used_kb" in b2 else "-")
        if b2
        else "invalid",
    )

    codes = [P.RESP_CODE_CONTACT_MSG_RECV, P.RESP_CODE_CONTACT_MSG_RECV_V3]
    txts = [P.TXT_TYPE_PLAIN, P.TXT_TYPE_CLI_DATA, P.TXT_TYPE_SIGNED, P.TXT_TYPE_CLI_COMMAND]
    for ci, code in enumerate(codes):
        for ti, txt in enumerate(txts):
            w = W()
            w.u8(code)
            if code == P.RESP_CODE_CONTACT_MSG_RECV_V3:
                w.u8(rng.byte())
                w.u16(0)
            w.raw(rng.bytes(6))
            w.u8(rng.byte())
            w.u8(txt)
            w.u32(1700000000)
            if txt == P.TXT_TYPE_SIGNED:
                w.raw(rng.bytes(4))
            w.snul("message body %d" % ti)
            f = w.take()
            emit("in.cmsg.%d.%d" % (ci, ti), f.hex())
            emit("out.cmsg.%d.%d" % (ci, ti), dump_cmsg(P.parse_contact_message(f)))
    for n in range(10, 18):
        f = bytearray(rng.bytes(n))
        f[0] = P.RESP_CODE_CONTACT_MSG_RECV_V3
        f = bytes(f)
        emit("in.cmsg_len.%d" % n, f.hex())
        emit("out.cmsg_len.%d" % n, dump_cmsg(P.parse_contact_message(f)))

    for ci in range(2):
        code = P.RESP_CODE_CHANNEL_MSG_RECV_V3 if ci else P.RESP_CODE_CHANNEL_MSG_RECV
        bodies = ["alice: hello there", "no separator here", "bob: s:AQID", "a: b: c"]
        for bi, body in enumerate(bodies):
            w = W()
            w.u8(code)
            if code == P.RESP_CODE_CHANNEL_MSG_RECV_V3:
                w.u8(rng.byte())
                w.u16(0)
            w.u8(bi)
            w.u8(rng.byte())
            w.u8(P.TXT_TYPE_PLAIN)
            w.u32(1700000123)
            w.snul(body)
            f = w.take()
            emit("in.chmsg.%d.%d" % (ci, bi), f.hex())
            emit("out.chmsg.%d.%d" % (ci, bi), dump_chmsg(P.parse_channel_message(f)))

    w = W()
    w.u8(P.RESP_CODE_CHANNEL_DATA_RECV).u8(0xF0).u16(0).u8(2).u8(0x41).u16(0x1234).u8(5)
    w.raw(rng.bytes(5))
    f = w.take()
    emit("in.chdata", f.hex())
    d = P.parse_channel_data(f)
    emit(
        "out.chdata",
        "snr=%s;idx=%d;plen=%d;hs=%d;dt=%d;dl=%d;pl=%s"
        % (
            F(d["SNR"]),
            d["channel_idx"],
            d["path_len"],
            d["path_hash_size"],
            d["data_type"],
            d["data_len"],
            d["payload"],
        )
        if d
        else "invalid",
    )

    w = W().u8(P.RESP_CODE_CHANNEL_INFO).u8(1).strunc("general", 32, 32).raw(rng.bytes(16))
    f = w.take()
    emit("in.chinfo", f.hex())
    c = P.parse_channel_info(f)
    emit(
        "out.chinfo",
        "%d/%s/%s" % (c["channel_idx"], Q(c["channel_name"]), c["channel_secret"].hex())
        if c
        else "invalid",
    )

    w = W().u8(P.RESP_CODE_SENT).u8(2).raw(rng.bytes(4)).u32(5000)
    f = w.take()
    emit("in.sent", f.hex())
    s = P.parse_sent_response(f)
    emit(
        "out.sent",
        "%d/%s/%d" % (s["type"], s["expected_ack"].hex(), s["suggested_timeout"])
        if s
        else "invalid",
    )

    for i in range(3):
        w = W().u8(P.PUSH_CODE_SEND_CONFIRMED).raw(rng.bytes(4))
        if i > 0:
            w.u32(1234 + i)
        f = w.take()
        emit("in.ack.%d" % i, f.hex())
        s = P.parse_push_send_confirmed(f)
        emit(
            "out.ack.%d" % i,
            "%s/%s" % (s["code"], I(s["trip_time_ms"]) if "trip_time_ms" in s else "-")
            if s
            else "invalid",
        )

    for i in range(4):
        f = bytearray([P.RESP_CODE_AUTO_ADD_CONFIG, (i * 7) & 0xFF])
        if i % 2:
            f.append(i)
        f = bytes(f)
        emit("in.autoadd.%d" % i, f.hex())
        a = P.parse_auto_add_config(f)
        emit("out.autoadd.%d" % i, "%d/%d" % (a["config"], a["max_hops"]) if a else "invalid")

    f1 = bytes([P.RESP_CODE_DEFAULT_FLOOD_SCOPE])
    emit("in.dscope.none", f1.hex())
    s1 = P.parse_default_flood_scope(f1)
    emit(
        "out.dscope.none",
        ("%s/%s" % (Q(s1["scope_name"]), s1["scope_key"]) if "scope_name" in s1 else "empty")
        if s1 is not None
        else "invalid",
    )
    w = W().u8(P.RESP_CODE_DEFAULT_FLOOD_SCOPE).strunc("#eu-region", 31, 31).raw(rng.bytes(16))
    f2 = w.take()
    emit("in.dscope.set", f2.hex())
    s2 = P.parse_default_flood_scope(f2)
    emit(
        "out.dscope.set",
        ("%s/%s" % (Q(s2["scope_name"]), s2["scope_key"]) if "scope_name" in s2 else "empty")
        if s2 is not None
        else "invalid",
    )

    for i in range(9):
        f = bytearray([P.RESP_CODE_ERR])
        if i < 8:
            f.append(i)
        e = P.parse_error_response(bytes(f))
        emit("out.err.%d" % i, "%d/%s" % (e["code"], Q(e["message"])))

    for i in range(6):
        w = W().u8(P.RESP_CODE_ADVERT_PATH).u32(1700000000 + i)
        w.u8(0xFF if i == 5 else P.encode_path_len(i, (i % 3) + 1))
        w.raw(rng.bytes(i * ((i % 3) + 1)))
        f = w.take()
        emit("in.advpath.%d" % i, f.hex())
        p = P.parse_advert_path_response(f)
        emit(
            "out.advpath.%d" % i,
            "%d/%d/%s/%d/%s"
            % (
                p["path_length"],
                p["path_hash_size"],
                p["path"].hex(),
                p["timestamp"],
                ",".join(p.get("hops", [])),
            )
            if p
            else "invalid",
        )

    for flags in range(4):
        hs = 1 << flags
        hops = 3
        w = W().u8(P.PUSH_CODE_TRACE_DATA).u8(0).u8(hops * hs).u8(flags)
        w.raw(rng.bytes(4)).u32(0)
        w.raw(rng.bytes(hops * hs))
        w.raw(rng.bytes(hops))
        f = w.take()
        emit("in.trace.%d" % flags, f.hex())
        t = P.parse_trace_response(f)
        if t is None:
            emit("out.trace.%d" % flags, "invalid")
        else:
            emit(
                "out.trace.%d" % flags,
                "tag=%s;hs=%d;hops=%s;snr=%s"
                % (
                    t["tag"].hex(),
                    t["path_hash_size"],
                    ",".join(h.hex() for h in t["path_hops"]),
                    ",".join(F(v) for v in t["snr_values"]),
                ),
            )

    for extra in range(3):
        w = W().u8(P.PUSH_CODE_STATUS_RESPONSE).u8(0).raw(rng.bytes(6))
        w.u16(4100).u16(3).i16(-120).i16(-95)
        w.u32(1000).u32(2000).u32(3000).u32(4000)
        w.u32(5).u32(6).u32(7).u32(8)
        w.u16(9).i16(-20)
        if extra >= 1:
            w.u16(11).u16(12)
        if extra >= 2:
            w.u32(13).u32(14)
        f = w.take()
        emit("in.status.%d" % extra, f.hex())
        emit("out.status.%d" % extra, dump_status(P.parse_status_response(f)))

    for kl in (2, 4, 6, 8):
        w = W().u8(P.RESP_CODE_BINARY_RESPONSE).u8(0).raw(rng.bytes(4)).u16(25).u16(3)
        for i in range(3):
            w.raw(rng.bytes(kl)).u32(1700000000 + i).u8(rng.byte())
        f = w.take()
        emit("in.neigh.%d" % kl, f.hex())
        n = P.parse_neighbors_response(f, kl)
        out = "total=%d;" % n["total"]
        for e in n["neighbors"]:
            out += "%s/%d/%s," % (e["pub_key_hex"], e["last_heard"], F(e["snr"]))
        emit("out.neigh.%d" % kl, out)

    for i in range(4):
        w = W().u8(P.RESP_CODE_CONTROL_DATA).raw(rng.bytes(2))
        w.u8(P.encode_path_len(i, 1))
        w.raw(rng.bytes(i))
        w.u8(0x90 | (i % 5))
        w.u8(rng.byte())
        w.u32(0xAABBCCDD)
        w.raw(rng.bytes(8))
        f = w.take()
        emit("in.disc.%d" % i, f.hex())
        d = P.parse_discover_response(f)
        emit(
            "out.disc.%d" % i,
            "%d/%s/%d/%s" % (d["node_type"], F(d["snr"]), d["tag"], d["pub_key_hex"])
            if d
            else "invalid",
        )

    for sub in range(3):
        w = W().u8(P.RESP_CODE_STATS).u8(sub).raw(rng.bytes(32))
        f = w.take()
        emit("in.stats.%d" % sub, f.hex())
        emit("out.stats.%d" % sub, dump_stats(P.parse_stats_response(f)))

    w = W().u8(P.RESP_CODE_ALLOWED_REPEAT_FREQ)
    for v in (863000, 870000, 902000, 928000, 0, 0, 111, 222):
        w.u32(v)
    f = w.take()
    emit("in.freq", f.hex())
    r = P.parse_allowed_repeat_freq(f)
    emit("out.freq", "".join("%d-%d," % (a, b) for a, b in r))

    cvs = ["gps:1,bat:80", "", "novalue", "a:1,b:2,c:3", "k:v:extra"]
    for i, cv in enumerate(cvs):
        w = W().u8(P.RESP_CODE_CUSTOM_VARS).s(cv)
        f = w.take()
        v = P.parse_custom_vars(f)
        emit("out.cvars.%d" % i, "".join("%s=%s," % (Q(k), Q(val)) for k, val in v.items()))

    w = W().u8(P.RESP_CODE_CURR_TIME).u32(1700001234)
    f = w.take()
    t = P.parse_curr_time(f)
    emit("out.currtime", I(t) if t is not None else "invalid")
    bad = bytes([P.RESP_CODE_CURR_TIME, 9, 9])
    t = P.parse_curr_time(bad)
    emit("out.currtime.short", I(t) if t is not None else "invalid")

    w = W().u8(P.RESP_CODE_CLI_REPLY).s("freq: 869.525").u8(0).u8(0)
    f = w.take()
    txt = P.parse_cli_reply(f)
    emit("out.clireply", Q(txt) if txt is not None else "invalid")

    for i in range(10):
        w = W()
        w.u8(rng.byte())
        w.u8(P.encode_path_len(i % 4, (i % 3) + 1))
        w.raw(rng.bytes((i % 4) * ((i % 3) + 1)))
        w.raw(rng.bytes(32))
        w.u32(1700000000)
        w.raw(rng.bytes(64))
        flags = (0x80 if i % 2 else 0) | (0x10 if i % 3 else 0) | (i % 4)
        w.u8(flags)
        if flags & 0x10:
            w.i32(50123456).i32(14456789)
        if flags & 0x80:
            w.snul("advname%d" % i)
        f = w.take()
        emit("in.advframe.%d" % i, f.hex())
        a = P.parse_advert_frame(f)
        emit(
            "out.advframe.%d" % i,
            "%s/%s/%d" % (a["pub_key_hex"], Q(a["name"]), a["type"]) if a else "invalid",
        )

    for i in range(24):
        raw = rng.bytes(1 + rng.below(140))
        emit("in.logpkt.%d" % i, raw.hex())
        emit("out.logpkt.%d" % i, dump_logpkt(mesh_crypto.decode_log_packet(raw)))

    for rt in range(4):
        w = W()
        w.u8((mesh_crypto.PAYLOAD_GROUP_TEXT << 2) | rt)
        if rt in (0, 3):
            w.raw(rng.bytes(4))
        w.u8(P.encode_path_len(2, 1))
        w.raw(rng.bytes(2))
        w.u8(0xAB)
        w.raw(rng.bytes(18))
        f = w.take()
        emit("in.grouptext.%d" % rt, f.hex())
        emit("out.grouptext.%d" % rt, dump_logpkt(mesh_crypto.decode_log_packet(f)))

    w = W()
    w.u8((mesh_crypto.PAYLOAD_ADVERT << 2) | 1)
    w.u8(P.encode_path_len(1, 1))
    w.raw(rng.bytes(1))
    w.raw(rng.bytes(32))
    w.u32(1700000000)
    w.raw(rng.bytes(64))
    w.u8(0x91)
    w.i32(50123456).i32(14456789)
    w.snul("repeater-x")
    f = w.take()
    emit("in.advpkt", f.hex())
    emit("out.advpkt", dump_logpkt(mesh_crypto.decode_log_packet(f)))


def test_lpp():
    lrng = Lcg(4242)
    types = [0, 1, 2, 3, 100, 101, 102, 103, 104, 113, 115, 116, 117, 118, 120, 121, 125, 128,
             130, 131, 132, 133, 134, 135, 136, 142, 200]

    def fmt(readings, with_names):
        out = []
        for r in readings:
            v = r["value"]
            if isinstance(v, dict):
                if "x" in v:
                    trip = (v["x"], v["y"], v["z"])
                elif "r" in v:
                    trip = (v["r"], v["g"], v["b"])
                else:
                    trip = (v["lat"], v["lon"], v["alt"])
                body = "%.6f,%.6f,%.6f" % trip
            else:
                body = "%.6f" % v
            if with_names:
                out.append("%d/%d/%s/%s/%s;" % (r["channel"], r["type"], r["name"], r["unit"], body))
            else:
                out.append("%d/%d/%s;" % (r["channel"], r["type"], body))
        return "".join(out)

    for t, tp in enumerate(types):
        w = W().u8(t + 1).u8(tp).raw(lrng.bytes(12))
        f = w.take()
        emit("in.lpp.%d" % t, f.hex())
        emit("out.lpp.%d" % t, fmt(P.parse_cayenne_lpp(f), True))
    for i in range(10):
        f = lrng.bytes(2 + lrng.below(24))
        emit("in.lpprand.%d" % i, f.hex())
        emit("out.lpprand.%d" % i, fmt(P.parse_cayenne_lpp(f), False))


def main():
    test_builders()
    test_helpers()
    test_parsers()
    test_lpp()
    sys.stdout.write("\n".join(OUT) + "\n")


if __name__ == "__main__":
    main()
