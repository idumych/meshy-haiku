// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Models.h"

#include <cmath>
#include <cstdio>

#include "Crypto.h"

namespace meshy {

const char* Channel::kPublicChannelPsk = "8b3387e9c5cdea6ac9e5edbaa115cd72";

std::string Contact::KeyPrefix(size_t bytes) const
{
    size_t n = public_key.size() < bytes ? public_key.size() : bytes;
    return ToHex(public_key.data(), n);
}

std::string Channel::DisplayName() const
{
    if (!name.empty())
        return name;
    char buf[32];
    snprintf(buf, sizeof(buf), "Channel %d", index);
    return buf;
}

int64_t CanonicalTimestamp(double t)
{
    return static_cast<int64_t>(llround(t * 1000000.0));
}

// Joins parts with NUL separators and returns the SHA-256 hex digest, matching
// upstream's _content_uid().
static std::string _ContentUid(const std::vector<std::string>& parts)
{
    std::string joined;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i)
            joined += '\0';
        joined += parts[i];
    }
    return ToHex(crypto::Sha256(joined));
}

static std::string _I64(int64_t v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    return buf;
}

std::string ContentUidDm(const std::string& contact_key_hex, const std::string& sender_key_hex,
    bool is_outgoing, double timestamp, const std::string& text,
    const std::string& room_sender_name)
{
    std::vector<std::string> parts;
    parts.push_back("dm");
    parts.push_back(contact_key_hex);
    parts.push_back(sender_key_hex);
    parts.push_back(is_outgoing ? "1" : "0");
    parts.push_back(_I64(CanonicalTimestamp(timestamp)));
    parts.push_back(room_sender_name);
    parts.push_back(text);
    return _ContentUid(parts);
}

std::string ContentUidChannel(int channel_index, const std::string& sender_name, bool is_outgoing,
    double timestamp, const std::string& text)
{
    std::vector<std::string> parts;
    parts.push_back("ch");
    parts.push_back(_I64(channel_index));
    parts.push_back(sender_name);
    parts.push_back(is_outgoing ? "1" : "0");
    parts.push_back(_I64(CanonicalTimestamp(timestamp)));
    parts.push_back(text);
    return _ContentUid(parts);
}

std::string NewUid()
{
    // RFC 4122 version 4, formatted exactly like Python's uuid4().
    ByteBuffer b = crypto::RandomBytes(16);
    if (b.size() < 16)
        b.resize(16, 0);
    b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);
    b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);
    std::string h = ToHex(b);
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4)
        + "-" + h.substr(20, 12);
}

}  // namespace meshy
