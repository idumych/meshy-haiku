// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Bytes.h"

namespace meshy {

static const char kHexLower[] = "0123456789abcdef";
static const char kHexUpper[] = "0123456789ABCDEF";

std::string ToHex(const uint8_t* data, size_t len)
{
    std::string out;
    out.resize(len * 2);
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = kHexLower[data[i] >> 4];
        out[i * 2 + 1] = kHexLower[data[i] & 0x0F];
    }
    return out;
}

std::string ToHexUpper(const uint8_t* data, size_t len)
{
    std::string out;
    out.resize(len * 2);
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = kHexUpper[data[i] >> 4];
        out[i * 2 + 1] = kHexUpper[data[i] & 0x0F];
    }
    return out;
}

static int _HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool FromHex(const std::string& hex, ByteBuffer& out)
{
    if (hex.size() % 2 != 0)
        return false;
    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = _HexVal(hex[i]);
        int lo = _HexVal(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

ByteBuffer FromHexOrEmpty(const std::string& hex)
{
    ByteBuffer out;
    if (!FromHex(hex, out))
        out.clear();
    return out;
}

// ─── Writer ──────────────────────────────────────────────────────

void ByteWriter::U16(uint16_t v)
{
    fBuf.push_back(static_cast<uint8_t>(v & 0xFF));
    fBuf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void ByteWriter::U32(uint32_t v)
{
    fBuf.push_back(static_cast<uint8_t>(v & 0xFF));
    fBuf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    fBuf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    fBuf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void ByteWriter::U16BE(uint16_t v)
{
    fBuf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    fBuf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void ByteWriter::U32BE(uint32_t v)
{
    fBuf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    fBuf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    fBuf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    fBuf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void ByteWriter::Bytes(const uint8_t* data, size_t len)
{
    if (len > 0 && data != NULL)
        fBuf.insert(fBuf.end(), data, data + len);
}

void ByteWriter::BytesPadded(const ByteBuffer& b, size_t max)
{
    size_t n = b.size() < max ? b.size() : max;
    Bytes(b.data(), n);
    Zeros(max - n);
}

void ByteWriter::Zeros(size_t count)
{
    fBuf.insert(fBuf.end(), count, 0);
}

void ByteWriter::StrTruncated(const std::string& s, size_t max_bytes, size_t pad_to)
{
    std::string t = TruncateUtf8(s, max_bytes);
    Str(t);
    if (pad_to > t.size())
        Zeros(pad_to - t.size());
}

// ─── Reader ──────────────────────────────────────────────────────

size_t ByteReader::_Take(size_t n)
{
    size_t avail = fPos <= fLen ? fLen - fPos : 0;
    if (n > avail) {
        fTruncated = true;
        return avail;
    }
    return n;
}

// Integer reads assemble the bytes that were actually available, little-endian,
// matching int.from_bytes() on a short BytesIO read.
uint8_t ByteReader::U8()
{
    size_t n = _Take(1);
    if (n == 0) return 0;
    return fData[fPos++];
}

uint16_t ByteReader::U16()
{
    size_t n = _Take(2);
    uint16_t v = 0;
    for (size_t i = 0; i < n; i++)
        v |= static_cast<uint16_t>(fData[fPos + i]) << (8 * i);
    fPos += n;
    return v;
}

uint32_t ByteReader::U32()
{
    size_t n = _Take(4);
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++)
        v |= static_cast<uint32_t>(fData[fPos + i]) << (8 * i);
    fPos += n;
    return v;
}

uint16_t ByteReader::U16BE()
{
    size_t n = _Take(2);
    uint16_t v = 0;
    for (size_t i = 0; i < n; i++)
        v = static_cast<uint16_t>((v << 8) | fData[fPos + i]);
    fPos += n;
    return v;
}

uint32_t ByteReader::U32BE()
{
    size_t n = _Take(4);
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++)
        v = (v << 8) | fData[fPos + i];
    fPos += n;
    return v;
}

int32_t ByteReader::I24BE()
{
    size_t n = _Take(3);
    if (n < 3) {
        fPos += n;
        return 0;
    }
    int32_t v = (static_cast<int32_t>(fData[fPos]) << 16)
        | (static_cast<int32_t>(fData[fPos + 1]) << 8) | static_cast<int32_t>(fData[fPos + 2]);
    fPos += 3;
    if (v & 0x800000)
        v -= 0x1000000;  // sign-extend 24-bit
    return v;
}

ByteBuffer ByteReader::Read(size_t n)
{
    size_t take = _Take(n);
    ByteBuffer out(fData + fPos, fData + fPos + take);
    fPos += take;
    return out;
}

void ByteReader::Skip(size_t n)
{
    fPos += _Take(n);
}

std::string ByteReader::ReadFixedStringStripNuls(size_t n)
{
    size_t take = _Take(n);
    n = take;
    const uint8_t* start = fData + fPos;
    fPos += n;
    std::string s = SanitizeUtf8(start, n);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '\0')
            out += s[i];
    }
    return out;
}

std::string ByteReader::ReadFixedStringAtNul(size_t n)
{
    size_t take = _Take(n);
    n = take;
    const uint8_t* start = fData + fPos;
    size_t len = 0;
    while (len < n && start[len] != 0)
        len++;
    fPos += n;
    return SanitizeUtf8(start, len);
}

std::string ByteReader::ReadRemainingString()
{
    // Remaining() is always in range; no short-read case here.
    size_t n = Remaining();
    const uint8_t* start = fData + fPos;
    fPos += n;
    while (n > 0 && start[n - 1] == 0)
        n--;
    return SanitizeUtf8(start, n);
}

std::string ByteReader::ReadRemainingStringStripBoth()
{
    // Remaining() is always in range; no short-read case here.
    size_t n = Remaining();
    const uint8_t* start = fData + fPos;
    fPos += n;
    size_t begin = 0;
    while (begin < n && start[begin] == 0)
        begin++;
    while (n > begin && start[n - 1] == 0)
        n--;
    return SanitizeUtf8(start + begin, n - begin);
}

ByteBuffer ByteReader::ReadRemaining()
{

    size_t n = Remaining();
    ByteBuffer out(fData + fPos, fData + fPos + n);
    fPos += n;
    return out;
}

std::string SanitizeUtf8(const uint8_t* data, size_t len)
{
    std::string out;
    out.reserve(len);
    size_t i = 0;
    while (i < len) {
        uint8_t c = data[i];
        size_t need;
        uint32_t cp;
        if (c < 0x80) {
            out += static_cast<char>(c);
            i++;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            need = 1;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            need = 2;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            need = 3;
            cp = c & 0x07;
        } else {
            i++;  // stray continuation or invalid lead byte
            continue;
        }
        if (i + need >= len) {
            i++;  // sequence runs past the end of the buffer
            continue;
        }
        bool ok = true;
        for (size_t k = 1; k <= need; k++) {
            if ((data[i + k] & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (data[i + k] & 0x3F);
        }
        if (!ok) {
            i++;  // drop the bad lead byte and resynchronise
            continue;
        }
        // Reject overlong forms, surrogates and out-of-range code points.
        bool valid = true;
        if (need == 1 && cp < 0x80) valid = false;
        if (need == 2 && cp < 0x800) valid = false;
        if (need == 3 && cp < 0x10000) valid = false;
        if (cp > 0x10FFFF) valid = false;
        if (cp >= 0xD800 && cp <= 0xDFFF) valid = false;
        if (!valid) {
            i++;
            continue;
        }
        out.append(reinterpret_cast<const char*>(data + i), need + 1);
        i += need + 1;
    }
    return out;
}

std::string TruncateUtf8(const std::string& text, size_t max_bytes)
{
    if (text.size() <= max_bytes)
        return text;
    size_t end = max_bytes;
    // Back off while we are inside a continuation sequence (0b10xxxxxx).
    while (end > 0 && (static_cast<uint8_t>(text[end]) & 0xC0) == 0x80)
        end--;
    return text.substr(0, end);
}

}  // namespace meshy
