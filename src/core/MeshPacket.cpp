// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MeshPacket.h"

#include <algorithm>
#include <set>

#include "Crypto.h"

namespace meshy {

static std::string _Trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\n\r\f\v");
    if (b == std::string::npos)
        return std::string();
    size_t e = s.find_last_not_of(" \t\n\r\f\v");
    return s.substr(b, e - b + 1);
}

std::string ChannelHashBytes(const ByteBuffer& psk)
{
    ByteBuffer h = crypto::Sha256(psk);
    if (h.empty())
        return std::string();
    return ToHex(h.data(), 1);
}

std::string ChannelHash(const std::string& psk_hex)
{
    return ChannelHashBytes(FromHexOrEmpty(psk_hex));
}

bool DeriveScopeKey(const std::string& name, ByteBuffer* out)
{
    std::string n = _Trim(name);
    if (n.empty() || n[0] == '$')
        return false;
    if (n == "0" || n == "None" || n == "*")
        return false;
    if (n[0] != '#')
        n = "#" + n;
    ByteBuffer h = crypto::Sha256(n);
    if (h.size() < 16)
        return false;
    h.resize(16);
    *out = h;
    return true;
}

uint16_t CalcTransportCode(const ByteBuffer& scope_key, uint8_t payload_type,
    const ByteBuffer& payload)
{
    ByteBuffer msg;
    msg.reserve(payload.size() + 1);
    msg.push_back(static_cast<uint8_t>(payload_type & 0x0F));
    msg.insert(msg.end(), payload.begin(), payload.end());

    ByteBuffer mac = crypto::HmacSha256(scope_key, msg);
    if (mac.size() < 2)
        return 1;
    uint16_t code = static_cast<uint16_t>(mac[0]) | (static_cast<uint16_t>(mac[1]) << 8);
    if (code == 0)
        return 1;
    if (code == 0xFFFF)
        return 0xFFFE;
    return code;
}

std::string ResolveRegionName(uint16_t transport_code, uint8_t payload_type,
    const ByteBuffer& payload, const std::vector<std::string>& region_names)
{
    std::set<std::string> unique;
    for (size_t i = 0; i < region_names.size(); i++) {
        ByteBuffer key;
        if (!DeriveScopeKey(region_names[i], &key))
            continue;
        if (CalcTransportCode(key, payload_type, payload) == transport_code)
            unique.insert(_Trim(region_names[i]));
    }
    std::string out;
    for (std::set<std::string>::const_iterator it = unique.begin(); it != unique.end(); ++it) {
        if (!out.empty())
            out += " / ";
        out += *it;
    }
    return out;
}

std::string ResolveRegionForLogPacket(const LogPacket& pkt,
    const std::vector<std::string>& region_names)
{
    if (pkt.transport_from.empty() || !pkt.valid)
        return std::string();
    ByteBuffer tc;
    if (!FromHex(pkt.transport_from, tc) || tc.size() < 2)
        return std::string();
    uint16_t code = static_cast<uint16_t>(tc[0]) | (static_cast<uint16_t>(tc[1]) << 8);
    return ResolveRegionName(code, pkt.payload_type, pkt.payload, region_names);
}

GroupTextPlain DecryptGroupText(const ByteBuffer& ciphertext, const ByteBuffer& psk)
{
    GroupTextPlain out;
    if (ciphertext.size() < 3)
        return out;

    ByteBuffer encrypted(ciphertext.begin() + 2, ciphertext.end());

    // HMAC key is the PSK zero-padded (or truncated) to 32 bytes.
    ByteBuffer key32 = psk;
    key32.resize(32, 0);
    ByteBuffer expected = crypto::HmacSha256(key32, encrypted);
    if (expected.size() < 2)
        return out;
    if (ciphertext[0] != expected[0] || ciphertext[1] != expected[1])
        return out;

    ByteBuffer key16 = psk;
    key16.resize(16, 0);
    ByteBuffer plaintext;
    if (!crypto::AesEcbDecrypt(key16, encrypted, plaintext))
        return out;
    if (plaintext.size() < 6)
        return out;

    ByteReader r(plaintext);
    out.timestamp = r.U32();
    out.flags = r.U8();
    std::string text = r.ReadRemainingString();

    // Split "sender: message" on the first ": ".
    size_t sep = text.find(": ");
    if (sep != std::string::npos) {
        out.sender = text.substr(0, sep);
        out.message = text.substr(sep + 2);
    } else {
        out.sender.clear();
        out.message = text;
    }
    out.valid = true;
    return out;
}

AdvertPayload ParseAdvertPayload(const ByteBuffer& payload)
{
    AdvertPayload a;
    if (payload.size() < 101)  // 32 + 4 + 64 + 1
        return a;
    a.public_key = ToHex(payload.data(), 32);
    ByteReader r(payload.data() + 32, 4);
    a.last_advert = r.U32();
    // bytes 36..99 hold the signature
    uint8_t flags = payload[100];
    a.type = flags & 0x0F;
    ParseAdvertAppData(payload, 101, flags, &a.has_location, &a.adv_lat, &a.adv_lon, &a.adv_name);
    a.valid = true;
    return a;
}

LogPacket DecodeLogPacket(const ByteBuffer& raw)
{
    LogPacket p;
    if (raw.size() < 2)
        return p;

    uint8_t header = raw[0];
    p.route_type = header & 0x03;
    p.payload_type = (header >> 2) & 0x0F;

    size_t offset = 1;

    // TC_FLOOD (0) and TC_DIRECT (3) carry two transport codes.
    if (p.route_type == 0 || p.route_type == 3) {
        if (raw.size() < offset + 4)
            return p;
        p.transport_from = ToHex(raw.data() + offset, 2);
        p.transport_to = ToHex(raw.data() + offset + 2, 2);
        offset += 4;
    }

    if (raw.size() < offset + 1)
        return p;
    uint8_t path_len_byte = raw[offset];
    offset += 1;

    int hop_count, hash_size;
    DecodePathLenByte(path_len_byte, &hop_count, &hash_size);
    size_t path_byte_length = static_cast<size_t>(hop_count) * static_cast<size_t>(hash_size);

    // Legacy fallback: the reserved hash size 4, or a path running past the end.
    if (hash_size == 4 || offset + path_byte_length > raw.size()) {
        size_t avail = raw.size() - offset;
        path_byte_length = path_len_byte;
        if (path_byte_length > avail)
            path_byte_length = avail;
        if (path_byte_length > 64)
            path_byte_length = 64;
        hop_count = static_cast<int>(path_byte_length);
        hash_size = 1;
    }
    p.path_hash_size = hash_size;

    for (int i = 0; i < hop_count; i++) {
        size_t hop_start = offset + static_cast<size_t>(i) * static_cast<size_t>(hash_size);
        size_t n = hash_size;
        if (hop_start >= raw.size())
            n = 0;
        else if (hop_start + n > raw.size())
            n = raw.size() - hop_start;
        p.path.push_back(n > 0 ? ToHex(raw.data() + hop_start, n) : std::string());
    }
    offset += path_byte_length;

    if (offset <= raw.size())
        p.payload.assign(raw.begin() + offset, raw.end());

    ByteBuffer hash_input;
    hash_input.reserve(p.payload.size() + 1);
    hash_input.push_back(p.payload_type);
    hash_input.insert(hash_input.end(), p.payload.begin(), p.payload.end());
    p.pkt_hash = ToHex(crypto::Sha256(hash_input)).substr(0, 16);

    p.valid = true;

    if (p.payload_type == PAYLOAD_ADVERT) {
        AdvertPayload adv = ParseAdvertPayload(p.payload);
        if (adv.valid) {
            p.advert = adv;
            p.has_advert = true;
        }
    } else if (p.payload_type == PAYLOAD_GROUP_TEXT && p.payload.size() >= 3) {
        p.channel_hash = ToHex(p.payload.data(), 1);
        p.ciphertext.assign(p.payload.begin() + 1, p.payload.end());
        p.has_channel = true;
    }

    return p;
}

}  // namespace meshy
