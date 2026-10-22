// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SMAZ decompression for short text messages.  Compressed messages arrive as
// an "s:" prefix followed by base64-encoded SMAZ data.

#ifndef MESHY_CORE_SMAZ_H
#define MESHY_CORE_SMAZ_H

#include <string>

#include "Bytes.h"

namespace meshy {
namespace smaz {

ByteBuffer Decompress(const ByteBuffer& data);

// If text starts with "s:", decodes base64 + SMAZ; otherwise returns text
// unchanged.  Any decoding failure also returns the original text.
std::string TryDecodePrefixed(const std::string& text);

// Standard and URL-safe base64, tolerant of missing padding.  Returns false
// when the input is not decodable.
bool Base64Decode(const std::string& in, ByteBuffer& out, bool strict);

}  // namespace smaz
}  // namespace meshy

#endif  // MESHY_CORE_SMAZ_H
