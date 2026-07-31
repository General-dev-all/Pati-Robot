// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <tl/expected.hpp>

#include "conversation/conversation_error.hpp"

namespace stackchan::conversation::base64 {

// Standard base64 (RFC 4648) encode/decode over mbedtls. Thin wrappers kept in
// their own translation unit so they stay independently testable.

std::vector<std::uint8_t> encode(std::span<const std::uint8_t> input);

// Encode directly into a caller-provided buffer to avoid per-call allocation
// on the hot audio path. Returns the number of bytes written (excluding the
// NUL terminator mbedtls appends), or an error if `out` is too small.
tl::expected<std::size_t, ConversationError> encode_into(std::span<const std::uint8_t> input,
                                                         std::span<char> out);

// Worst-case encoded length (including the NUL terminator) for `input_len`
// input bytes.
constexpr std::size_t encoded_size(std::size_t input_len) noexcept
{
    return 4 * ((input_len + 2) / 3) + 1;
}

tl::expected<std::vector<std::uint8_t>, ConversationError> decode(std::string_view input);

} // namespace stackchan::conversation::base64
