// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Crypto.h"

#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace meshy {
namespace crypto {

ByteBuffer Sha256(const uint8_t* data, size_t len)
{
    ByteBuffer out(SHA256_DIGEST_LENGTH);
    unsigned int outlen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == NULL)
        return ByteBuffer();
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1
        && EVP_DigestUpdate(ctx, data, len) == 1
        && EVP_DigestFinal_ex(ctx, out.data(), &outlen) == 1) {
        EVP_MD_CTX_free(ctx);
        out.resize(outlen);
        return out;
    }
    EVP_MD_CTX_free(ctx);
    return ByteBuffer();
}

ByteBuffer HmacSha256(const ByteBuffer& key, const uint8_t* data, size_t len)
{
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int maclen = 0;
    // An empty key is legal for HMAC but OpenSSL wants a non-null pointer.
    static const unsigned char kEmpty = 0;
    const unsigned char* kptr = key.empty() ? &kEmpty : key.data();
    if (HMAC(EVP_sha256(), kptr, static_cast<int>(key.size()), data, len, mac, &maclen) == NULL)
        return ByteBuffer();
    return ByteBuffer(mac, mac + maclen);
}

static bool _AesEcb(const ByteBuffer& key16, const ByteBuffer& in, ByteBuffer& out, bool encrypt)
{
    out.clear();
    if (key16.size() != 16 || in.empty() || (in.size() % 16) != 0)
        return false;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL)
        return false;

    bool ok = false;
    out.resize(in.size() + 16);
    int outlen = 0;
    int finallen = 0;
    const EVP_CIPHER* cipher = EVP_aes_128_ecb();

    if ((encrypt ? EVP_EncryptInit_ex(ctx, cipher, NULL, key16.data(), NULL)
                 : EVP_DecryptInit_ex(ctx, cipher, NULL, key16.data(), NULL))
        == 1) {
        // The firmware does raw block work with no padding.
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        int step = encrypt
            ? EVP_EncryptUpdate(ctx, out.data(), &outlen, in.data(), static_cast<int>(in.size()))
            : EVP_DecryptUpdate(ctx, out.data(), &outlen, in.data(), static_cast<int>(in.size()));
        if (step == 1) {
            int fin = encrypt ? EVP_EncryptFinal_ex(ctx, out.data() + outlen, &finallen)
                              : EVP_DecryptFinal_ex(ctx, out.data() + outlen, &finallen);
            if (fin == 1) {
                out.resize(static_cast<size_t>(outlen + finallen));
                ok = true;
            }
        }
    }

    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        out.clear();
    return ok;
}

bool AesEcbDecrypt(const ByteBuffer& key16, const ByteBuffer& in, ByteBuffer& out)
{
    return _AesEcb(key16, in, out, false);
}

bool AesEcbEncrypt(const ByteBuffer& key16, const ByteBuffer& in, ByteBuffer& out)
{
    return _AesEcb(key16, in, out, true);
}

ByteBuffer RandomBytes(size_t n)
{
    ByteBuffer out(n);
    if (n > 0 && RAND_bytes(out.data(), static_cast<int>(n)) != 1)
        out.assign(n, 0);
    return out;
}

uint32_t RandomU32()
{
    ByteBuffer b = RandomBytes(4);
    if (b.size() < 4)
        return 0;
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8)
        | (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
}

}  // namespace crypto
}  // namespace meshy
