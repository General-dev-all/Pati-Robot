// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <string>
#include <string_view>

#include <esp_log.h>
#include <esp_websocket_client.h>

namespace stackchan::conversation {

// Append user-configured extra HTTP headers to the WebSocket upgrade request.
// Used to carry e.g. Cloudflare Access service tokens (CF-Access-Client-Id /
// CF-Access-Client-Secret) in front of a proxied conversation API.
//
// `block` is a newline-separated list of "Name: value" lines (LF or CRLF).
// Blank lines and lines without a ':' are skipped; names/values are trimmed.
// Must be called AFTER esp_websocket_client_init() and BEFORE start() — it
// appends to the client's header buffer (coexisting with cfg.headers and any
// other esp_websocket_client_append_header calls).
inline void apply_extra_ws_headers(esp_websocket_client_handle_t client, std::string_view block)
{
    auto trim = [](std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
        return s;
    };

    std::size_t pos = 0;
    while (pos < block.size()) {
        const std::size_t nl = block.find('\n', pos);
        std::string_view line = block.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = (nl == std::string_view::npos) ? block.size() : nl + 1;

        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        const std::string name{trim(line.substr(0, colon))};
        const std::string value{trim(line.substr(colon + 1))};
        if (name.empty()) {
            continue;
        }
        esp_websocket_client_append_header(client, name.c_str(), value.c_str());
        ESP_LOGI("conv-hdr", "extra upgrade header: %s", name.c_str());
    }
}

} // namespace stackchan::conversation
