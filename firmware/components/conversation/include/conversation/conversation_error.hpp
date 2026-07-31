// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#pragma once

namespace stackchan::conversation {

enum class ConversationError {
    NotConnected,
    TransportInit,
    SendFailed,
    ProtocolError,
    ServerError,
    Timeout,
    InvalidState,
    OutOfMemory,
};

} // namespace stackchan::conversation
