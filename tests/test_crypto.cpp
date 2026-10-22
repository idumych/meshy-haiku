// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Known-answer tests for the crypto primitives, plus a GroupText round trip.
// The differential test cannot cover AES because the reference environment has
// no pycryptodome, so these vectors stand in for it.

#include <cstdio>
#include <string>

#include "Bytes.h"
#include "Crypto.h"
#include "MeshPacket.h"

using namespace meshy;

static int g_failures = 0;
static int g_checks = 0;

static void Check(const std::string& name, const std::string& got, const std::string& want)
{
    g_checks++;
    if (got != want) {
        g_failures++;
        printf("FAIL %s\n  got  %s\n  want %s\n", name.c_str(), got.c_str(), want.c_str());
    }
}

static void CheckTrue(const std::string& name, bool cond)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL %s\n", name.c_str());
    }
}

int main()
{
    // ── SHA-256 (FIPS 180-4) ──
    Check("sha256.abc", ToHex(crypto::Sha256(std::string("abc"))),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Check("sha256.empty", ToHex(crypto::Sha256(std::string(""))),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // ── HMAC-SHA256 (RFC 4231 case 1 and 2) ──
    Check("hmac.rfc4231.1",
        ToHex(crypto::HmacSha256(ByteBuffer(20, 0x0b),
            reinterpret_cast<const uint8_t*>("Hi There"), 8)),
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    {
        ByteBuffer key(reinterpret_cast<const uint8_t*>("Jefe"),
            reinterpret_cast<const uint8_t*>("Jefe") + 4);
        const char* msg = "what do ya want for nothing?";
        Check("hmac.rfc4231.2",
            ToHex(crypto::HmacSha256(key, reinterpret_cast<const uint8_t*>(msg), 28)),
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    }

    // ── AES-128-ECB (FIPS 197 and NIST SP 800-38A) ──
    {
        ByteBuffer key = FromHexOrEmpty("000102030405060708090a0b0c0d0e0f");
        ByteBuffer pt = FromHexOrEmpty("00112233445566778899aabbccddeeff");
        ByteBuffer ct;
        CheckTrue("aes.fips197.encrypt_ok", crypto::AesEcbEncrypt(key, pt, ct));
        Check("aes.fips197", ToHex(ct), "69c4e0d86a7b0430d8cdb78070b4c55a");
        ByteBuffer back;
        CheckTrue("aes.fips197.decrypt_ok", crypto::AesEcbDecrypt(key, ct, back));
        Check("aes.fips197.roundtrip", ToHex(back), ToHex(pt));
    }
    {
        ByteBuffer key = FromHexOrEmpty("2b7e151628aed2a6abf7158809cf4f3c");
        ByteBuffer pt = FromHexOrEmpty("6bc1bee22e409f96e93d7e117393172a"
                                       "ae2d8a571e03ac9c9eb76fac45af8e51");
        ByteBuffer ct;
        CheckTrue("aes.38a.encrypt_ok", crypto::AesEcbEncrypt(key, pt, ct));
        Check("aes.38a.2blocks", ToHex(ct),
            "3ad77bb40d7a3660a89ecaf32466ef97f5d3d58503b9699de785895a96fdbaaf");
    }
    // Misuse must fail cleanly rather than read out of bounds.
    {
        ByteBuffer out;
        CheckTrue("aes.badkeylen", !crypto::AesEcbDecrypt(ByteBuffer(8, 1), ByteBuffer(16, 2), out));
        CheckTrue("aes.badblocklen",
            !crypto::AesEcbDecrypt(ByteBuffer(16, 1), ByteBuffer(17, 2), out));
        CheckTrue("aes.empty", !crypto::AesEcbDecrypt(ByteBuffer(16, 1), ByteBuffer(), out));
    }

    // ── Channel hash ──
    // SHA-256(psk)[0] for an all-zero 16-byte PSK.
    {
        ByteBuffer psk(16, 0);
        ByteBuffer h = crypto::Sha256(psk);
        Check("channelhash.zero", ChannelHashBytes(psk), ToHex(h.data(), 1));
    }

    // ── GroupText round trip ──
    // Builds a payload exactly as the firmware does, then decrypts it.
    {
        ByteBuffer psk = FromHexOrEmpty("8b3387e9c5cdea6ac9e5edbaa115cd72");
        ByteWriter plain;
        plain.U32(1700000000u);  // timestamp
        plain.U8(0);             // flags
        plain.Str("alice: hello haiku");
        ByteBuffer pt = plain.Take();
        pt.resize(((pt.size() + 15) / 16) * 16, 0);  // zero-pad to a block

        ByteBuffer key16 = psk;
        key16.resize(16, 0);
        ByteBuffer ct;
        CheckTrue("grouptext.encrypt_ok", crypto::AesEcbEncrypt(key16, pt, ct));

        ByteBuffer key32 = psk;
        key32.resize(32, 0);
        ByteBuffer mac = crypto::HmacSha256(key32, ct);

        ByteBuffer payload;
        payload.push_back(mac[0]);
        payload.push_back(mac[1]);
        payload.insert(payload.end(), ct.begin(), ct.end());

        GroupTextPlain g = DecryptGroupText(payload, psk);
        CheckTrue("grouptext.valid", g.valid);
        Check("grouptext.sender", g.sender, "alice");
        Check("grouptext.message", g.message, "hello haiku");
        Check("grouptext.timestamp", std::to_string(g.timestamp), "1700000000");

        // A corrupted MAC must be rejected.
        ByteBuffer bad = payload;
        bad[0] ^= 0xFF;
        CheckTrue("grouptext.badmac", !DecryptGroupText(bad, psk).valid);

        // So must the wrong key.
        ByteBuffer wrong = FromHexOrEmpty("00112233445566778899aabbccddeeff");
        CheckTrue("grouptext.wrongkey", !DecryptGroupText(payload, wrong).valid);

        // Truncated payloads must not read out of bounds.
        for (size_t n = 0; n < 4; n++) {
            ByteBuffer t(payload.begin(), payload.begin() + n);
            CheckTrue("grouptext.trunc" + std::to_string(n), !DecryptGroupText(t, psk).valid);
        }
    }

    // ── UTF-8 sanitiser ──
    {
        const uint8_t bad1[] = { 'a', 0xC3, 'b' };          // truncated 2-byte seq
        Check("utf8.truncated", SanitizeUtf8(bad1, 3), "ab");
        const uint8_t bad2[] = { 0x80, 'x' };               // stray continuation
        Check("utf8.stray", SanitizeUtf8(bad2, 2), "x");
        const uint8_t ok[] = { 0xC3, 0xA9 };                // é
        Check("utf8.valid", SanitizeUtf8(ok, 2), "\xc3\xa9");
        const uint8_t sur[] = { 0xED, 0xA0, 0x80 };         // UTF-16 surrogate
        Check("utf8.surrogate", SanitizeUtf8(sur, 3), "");
        const uint8_t over[] = { 0xC0, 0xAF };              // overlong '/'
        Check("utf8.overlong", SanitizeUtf8(over, 2), "");
    }

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
