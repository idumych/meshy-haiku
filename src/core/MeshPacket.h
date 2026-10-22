// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Raw MeshCore packet decoding and channel crypto — the subset Meshy needs:
// channel hash calculation, GroupText decryption, transport/region codes and
// header/path parsing for LOG_DATA (0x88) frames.
// Ported from upstream Meshy src/mesh_crypto.py.

#ifndef MESHY_CORE_MESHPACKET_H
#define MESHY_CORE_MESHPACKET_H

#include <string>
#include <vector>

#include "Bytes.h"
#include "Protocol.h"

namespace meshy {

// Payload type constants (header bits 2-5).
static const uint8_t PAYLOAD_ADVERT = 0x04;
static const uint8_t PAYLOAD_GROUP_TEXT = 0x05;

struct AdvertPayload {
    bool valid = false;
    std::string public_key;
    uint8_t type = 0;
    std::string adv_name;
    uint32_t last_advert = 0;
    bool has_location = false;
    double adv_lat = 0.0;
    double adv_lon = 0.0;
};

struct GroupTextPlain {
    bool valid = false;
    std::string sender;
    std::string message;
    uint32_t timestamp = 0;
    uint8_t flags = 0;
};

struct LogPacket {
    bool valid = false;
    uint8_t payload_type = 0;
    uint8_t route_type = 0;
    std::vector<std::string> path;  // hex per hop
    int path_hash_size = 1;
    std::string pkt_hash;           // first 16 hex chars of SHA-256
    std::string transport_from;     // hex, empty when absent
    std::string transport_to;
    ByteBuffer payload;

    bool has_advert = false;
    AdvertPayload advert;

    bool has_channel = false;
    std::string channel_hash;  // 2 hex chars
    ByteBuffer ciphertext;     // mac + encrypted data
};

// 1-byte channel hash from a PSK hex string: SHA-256(psk)[0] as 2 hex chars.
std::string ChannelHash(const std::string& psk_hex);
std::string ChannelHashBytes(const ByteBuffer& psk);

// 16-byte scope key for a public region name, mirroring the firmware's
// TransportKeyStore::getAutoKeyFor (SHA-256 of the name) with RegionMap's rule
// that prepends '#'.  Returns empty for blank names and for '$'-prefixed
// (private) regions, whose keys are not derivable from the name.
bool DeriveScopeKey(const std::string& name, ByteBuffer* out);

// 2-byte transport code: HMAC-SHA256 over [payload_type] || payload keyed with
// the scope key; first two MAC bytes little-endian, with the reserved values
// 0x0000/0xFFFF rewritten to 0x0001/0xFFFE.
uint16_t CalcTransportCode(const ByteBuffer& scope_key, uint8_t payload_type,
    const ByteBuffer& payload);

// Reverse-resolves a transport code to a region name.  Colliding names are
// joined with " / "; an empty string means no match.
std::string ResolveRegionName(uint16_t transport_code, uint8_t payload_type,
    const ByteBuffer& payload, const std::vector<std::string>& region_names);
std::string ResolveRegionForLogPacket(const LogPacket& pkt,
    const std::vector<std::string>& region_names);

// Decrypts a GroupText payload (after the channel-hash byte) with the channel
// PSK: 2-byte HMAC prefix then AES-128-ECB ciphertext.
GroupTextPlain DecryptGroupText(const ByteBuffer& ciphertext, const ByteBuffer& psk);

AdvertPayload ParseAdvertPayload(const ByteBuffer& payload);
LogPacket DecodeLogPacket(const ByteBuffer& raw);

}  // namespace meshy

#endif  // MESHY_CORE_MESHPACKET_H
