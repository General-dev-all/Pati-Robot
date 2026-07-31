// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include "base64.hpp"

#include <mbedtls/base64.h>

namespace stackchan::conversation::base64 {

std::vector<std::uint8_t> encode(std::span<const std::uint8_t> input)
{
    std::vector<std::uint8_t> out(encoded_size(input.size()));
    std::size_t written = 0;
    const int rc = mbedtls_base64_encode(out.data(), out.size(), &written, input.data(), input.size());
    if (rc != 0) {
        return {};
    }
    out.resize(written);
    return out;
}

tl::expected<std::size_t, ConversationError> encode_into(std::span<const std::uint8_t> input, std::span<char> out)
{
    std::size_t written = 0;
    const int rc = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out.data()), out.size(), &written,
                                         input.data(), input.size());
    if (rc != 0) {
        return tl::unexpected{ConversationError::OutOfMemory};
    }
    return written;
}

tl::expected<std::vector<std::uint8_t>, ConversationError> decode(std::string_view input)
{
    const auto* src = reinterpret_cast<const unsigned char*>(input.data());

    // First call with a null destination just reports the required size.
    std::size_t needed = 0;
    mbedtls_base64_decode(nullptr, 0, &needed, src, input.size());

    std::vector<std::uint8_t> out(needed);
    std::size_t written = 0;
    const int rc = mbedtls_base64_decode(out.data(), out.size(), &written, src, input.size());
    if (rc != 0) {
        return tl::unexpected{ConversationError::ProtocolError};
    }
    out.resize(written);
    return out;
}

} // namespace stackchan::conversation::base64
