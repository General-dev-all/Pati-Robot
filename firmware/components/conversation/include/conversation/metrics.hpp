// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#pragma once

// Cheap running-statistics aggregator for the audio pipeline. Tracks
// count / sum / min / max of a metric so we can dump a one-line summary at
// the end of each conversation turn without keeping a per-sample log.
// Lives in the conversation component so both the backend clients (gemini
// / openai / xiaozhi) and main/conversation_task can share the same type
// (main `PRIV_REQUIRES conversation`).

#include <algorithm>
#include <cstddef>
#include <limits>

namespace stackchan::conversation {

template <typename T = double>
struct Stats {
    std::size_t count = 0;
    T sum{};
    T min_v = std::numeric_limits<T>::max();
    T max_v = std::numeric_limits<T>::lowest();

    void record(T v)
    {
        ++count;
        sum += v;
        min_v = std::min(min_v, v);
        max_v = std::max(max_v, v);
    }

    double mean() const
    {
        return count == 0 ? 0.0 : static_cast<double>(sum) / static_cast<double>(count);
    }
    T min() const { return count == 0 ? T{} : min_v; }
    T max() const { return count == 0 ? T{} : max_v; }

    void reset() { *this = Stats{}; }
};

} // namespace stackchan::conversation
