// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Byte buffer primitives shared by the protocol layer.  Little-endian helpers
// mirror Python's struct '<' formats; the readers are bounds-checked and set a
// failure flag instead of throwing, so parsers can bail out like the upstream
// code does when a frame is truncated.

#ifndef MESHY_CORE_BYTES_H
#define MESHY_CORE_BYTES_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace meshy {

typedef std::vector<uint8_t> ByteBuffer;

// ─── Hex helpers ─────────────────────────────────────────────────

std::string ToHex(const uint8_t* data, size_t len);
inline std::string ToHex(const ByteBuffer& b) { return ToHex(b.data(), b.size()); }
std::string ToHexUpper(const uint8_t* data, size_t len);
inline std::string ToHexUpper(const ByteBuffer& b) { return ToHexUpper(b.data(), b.size()); }

// Returns false when the string is not valid hex (odd length or bad digit).
bool FromHex(const std::string& hex, ByteBuffer& out);
ByteBuffer FromHexOrEmpty(const std::string& hex);

// ─── Writer ──────────────────────────────────────────────────────

class ByteWriter {
public:
    ByteWriter() {}
    explicit ByteWriter(size_t reserve) { fBuf.reserve(reserve); }

    void U8(uint8_t v) { fBuf.push_back(v); }
    void U16(uint16_t v);
    void U32(uint32_t v);
    void I16(int16_t v) { U16(static_cast<uint16_t>(v)); }
    void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
    void U16BE(uint16_t v);
    void U32BE(uint32_t v);

    void Bytes(const uint8_t* data, size_t len);
    void Bytes(const ByteBuffer& b) { Bytes(b.data(), b.size()); }
    // Appends at most `max` bytes of b, zero-padding to exactly `max`.
    void BytesPadded(const ByteBuffer& b, size_t max);
    void Zeros(size_t count);

    void Str(const std::string& s) { Bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
    void StrNul(const std::string& s) { Str(s); U8(0); }
    // Writes s truncated to at most max_bytes of *valid* UTF-8 (no split
    // sequences), then zero-pads the field to pad_to bytes when pad_to > 0.
    void StrTruncated(const std::string& s, size_t max_bytes, size_t pad_to = 0);

    const ByteBuffer& Buffer() const { return fBuf; }
    ByteBuffer Take() { return fBuf; }
    size_t Size() const { return fBuf.size(); }

private:
    ByteBuffer fBuf;
};

// ─── Reader ──────────────────────────────────────────────────────

// Reads follow Python io.BytesIO semantics, which the upstream parsers rely
// on: a read past the end returns the bytes that remain instead of failing,
// and an integer read assembles whatever bytes were available (so a fully
// exhausted read yields 0, like int.from_bytes(b"")).  Truncated() reports
// whether any read came up short, for callers that want to reject the frame.
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t len)
        : fData(data), fLen(len), fPos(0), fTruncated(false)
    {
    }
    explicit ByteReader(const ByteBuffer& b)
        : fData(b.data()), fLen(b.size()), fPos(0), fTruncated(false)
    {
    }

    uint8_t U8();
    uint16_t U16();
    uint32_t U32();
    int16_t I16() { return static_cast<int16_t>(U16()); }
    int32_t I32() { return static_cast<int32_t>(U32()); }
    int8_t I8() { return static_cast<int8_t>(U8()); }
    uint16_t U16BE();
    uint32_t U32BE();
    int16_t I16BE() { return static_cast<int16_t>(U16BE()); }
    // 24-bit big-endian signed, as used by Cayenne LPP GPS.
    int32_t I24BE();

    ByteBuffer Read(size_t n);
    // The three NUL conventions the wire format actually uses.  All of them
    // drop invalid UTF-8 first, matching Python's decode(errors="ignore").
    //
    // Fixed field with embedded NUL padding removed throughout — contact
    // names, device-info strings, the flood-scope name.
    std::string ReadFixedStringStripNuls(size_t n);
    // Fixed field truncated at the first NUL — channel names.
    std::string ReadFixedStringAtNul(size_t n);
    // Rest of the buffer with trailing NULs removed — self-info name, DM
    // text, CLI replies.
    std::string ReadRemainingString();
    // Rest of the buffer with NULs removed at both ends — channel message text.
    std::string ReadRemainingStringStripBoth();
    ByteBuffer ReadRemaining();

    void Skip(size_t n);

    bool Ok() const { return !fTruncated; }
    bool Truncated() const { return fTruncated; }
    size_t Remaining() const { return fPos <= fLen ? fLen - fPos : 0; }
    size_t Pos() const { return fPos; }
    bool Has(size_t n) const { return fPos + n <= fLen; }

private:
    // Clamps n to what remains, flagging a short read.
    size_t _Take(size_t n);

    const uint8_t* fData;
    size_t fLen;
    size_t fPos;
    bool fTruncated;
};

// Truncates to at most max_bytes without splitting a UTF-8 sequence.
std::string TruncateUtf8(const std::string& text, size_t max_bytes);

// Drops malformed UTF-8 byte sequences, keeping the valid remainder.  This
// mirrors Python's bytes.decode("utf-8", errors="ignore"), which the upstream
// parsers rely on, and keeps invalid bytes out of Haiku's text views.
std::string SanitizeUtf8(const uint8_t* data, size_t len);

}  // namespace meshy

#endif  // MESHY_CORE_BYTES_H
