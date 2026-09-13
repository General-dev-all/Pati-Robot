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

// 🔴 HAZIR TAMPONA COZ — `encode_into`'nun ikizi.
//
// 13.09.2026, gercek kartta olculdu: gelen ses parcasi basina
// `decode()` bir vector ayiriyor, cagiran taraf IKINCI bir vector
// ayirip aralarinda 13 KB kopyaliyordu. Parca basina iki ayirma, bir
// kopya, bir serbest birakma — saniyede bes kez, konusma boyunca.
//
// Olculen bedeli: cozme suresi ortalama 36 ms, en kotu 103 ms ve
// aralarinda ON SEKIZ KAT oynama. O oynama isin agirligindan degil
// yigin cekismesinden geliyor; is agir olsa hep ayni sururdu.
// Hoparlorun tamponu o sicramalarda bir an bosaliyor ve yerine
// sessizlik basiliyor — kullanicinin "bes saniyelik cumlenin 0,3
// saniyesi kesiliyor" dedigi sey.
//
// ⚠️ Gonderme yolu bu isi ZATEN dogru yapiyordu (encode_into +
// onceden ayrilmis b64_scratch_). Asimetri kodda duruyordu ve
// yalnizca TX tarafi olculdugu icin gorulmemisti.
std::size_t decoded_size(std::string_view input) noexcept;

tl::expected<std::size_t, ConversationError> decode_into(std::string_view input,
                                                         std::span<std::uint8_t> out);

} // namespace stackchan::conversation::base64
