// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Smaz.h"

#include <cstring>

namespace meshy {
namespace smaz {

#include "SmazTable.inc"

static const uint8_t kVerbatimSingle = 254;
static const uint8_t kVerbatimRun = 255;

ByteBuffer Decompress(const ByteBuffer& data)
{
    ByteBuffer out;
    size_t i = 0;
    while (i < data.size()) {
        uint8_t code = data[i];
        if (code == kVerbatimSingle) {
            if (i + 1 >= data.size())
                break;
            out.push_back(data[i + 1]);
            i += 2;
        } else if (code == kVerbatimRun) {
            if (i + 1 >= data.size())
                break;
            size_t length = static_cast<size_t>(data[i + 1]) + 1;
            size_t end = i + 2 + length;
            if (end > data.size())
                break;
            out.insert(out.end(), data.begin() + (i + 2), data.begin() + end);
            i = end;
        } else if (code < kSmazCodebookSize) {
            const char* entry = kSmazCodebook[code];
            out.insert(out.end(), entry, entry + strlen(entry));
            i += 1;
        } else {
            break;
        }
    }
    return out;
}

static int _B64Val(char c, bool urlsafe)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || (urlsafe && c == '-')) return 62;
    if (c == '/' || (urlsafe && c == '_')) return 63;
    return -1;
}

bool Base64Decode(const std::string& in, ByteBuffer& out, bool strict)
{
    out.clear();
    std::string s;
    s.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '=')
            break;  // padding ends the data
        if (!strict && (c == '\n' || c == '\r'))
            continue;
        s.push_back(c);
    }
    // A length of 1 mod 4 can never be produced by a valid encoder.
    if (s.size() % 4 == 1)
        return false;

    // Lenient mode mirrors Python's base64.b64decode(validate=False), which
    // silently discards characters outside the alphabet; strict mode rejects
    // them, as validate=True does.
    if (!strict) {
        std::string filtered;
        filtered.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++) {
            if (_B64Val(s[i], true) >= 0)
                filtered.push_back(s[i]);
        }
        s.swap(filtered);
        if (s.size() % 4 == 1)
            return false;
    }

    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < s.size(); i++) {
        int v = _B64Val(s[i], !strict);
        if (v < 0) {
            out.clear();
            return false;
        }
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

std::string TryDecodePrefixed(const std::string& text)
{
    size_t start = text.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos)
        return text;
    std::string stripped = text.substr(start);
    if (stripped.size() <= 2 || stripped.compare(0, 2, "s:") != 0)
        return text;

    std::string encoded = stripped.substr(2);
    ByteBuffer compressed;
    // Upstream tries strict base64 first, then a URL-safe/unpadded retry.
    if (!Base64Decode(encoded, compressed, true)) {
        if (!Base64Decode(encoded, compressed, false))
            return text;
    }
    ByteBuffer plain = Decompress(compressed);
    return std::string(reinterpret_cast<const char*>(plain.data()), plain.size());
}

}  // namespace smaz
}  // namespace meshy
