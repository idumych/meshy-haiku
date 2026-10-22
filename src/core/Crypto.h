// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The crypto subset MeshCore needs: SHA-256, HMAC-SHA-256 and AES-128-ECB.
// Backed by OpenSSL, which ships with Haiku and is present on Linux for the
// host-side tests.  Everything is wrapped here so the backend can be swapped
// without touching the protocol layer.

#ifndef MESHY_CORE_CRYPTO_H
#define MESHY_CORE_CRYPTO_H

#include <string>

#include "Bytes.h"

namespace meshy {
namespace crypto {

// 32-byte digest.
ByteBuffer Sha256(const uint8_t* data, size_t len);
inline ByteBuffer Sha256(const ByteBuffer& b) { return Sha256(b.data(), b.size()); }
inline ByteBuffer Sha256(const std::string& s)
{
    return Sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// 32-byte MAC.
ByteBuffer HmacSha256(const ByteBuffer& key, const uint8_t* data, size_t len);
inline ByteBuffer HmacSha256(const ByteBuffer& key, const ByteBuffer& data)
{
    return HmacSha256(key, data.data(), data.size());
}

// ECB with no padding; `in` must be a multiple of 16 bytes or the call fails.
// Returns false and leaves `out` empty on error.
bool AesEcbDecrypt(const ByteBuffer& key16, const ByteBuffer& in, ByteBuffer& out);
bool AesEcbEncrypt(const ByteBuffer& key16, const ByteBuffer& in, ByteBuffer& out);

// Cryptographically secure random bytes (used for packet-hash salts and tags).
ByteBuffer RandomBytes(size_t n);
uint32_t RandomU32();

}  // namespace crypto
}  // namespace meshy

#endif  // MESHY_CORE_CRYPTO_H
