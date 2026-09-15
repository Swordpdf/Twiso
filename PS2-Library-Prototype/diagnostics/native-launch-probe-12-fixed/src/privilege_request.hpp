// etaHEN/OnionHEN app-jailbreak request probe. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace privilege_probe {
// etaHEN watches etahen_jailbreak; OnionHEN watches both etahen|onionhen
// (arch.md), so publishing to the etaHEN name works on both HENs.
inline constexpr char request_path[] = "/download0/etahen_jailbreak";
inline constexpr char staged_request_path[] = "/download0/etahen_jailbreak.tmp";
// The HEN daemon removes the request before its per-process jailbreak is finished.
// Keep a bounded post-consume grace period so the frontend does not race the
// launch service while it still reports the caller as a non-system process.
inline constexpr std::size_t max_polls = 600;
inline constexpr std::size_t post_consume_max_polls = 450;

int process_id() noexcept;
int effective_user_id() noexcept;
int remove_request(const char* path) noexcept;
int open_request(const char* path) noexcept;
int make_request_readable(int descriptor) noexcept;
int write_request(int descriptor, const void* data, std::size_t size) noexcept;
int sync_request(int descriptor) noexcept;
int close_request(int descriptor) noexcept;
int publish_request(const char* source, const char* target) noexcept;
int request_exists(const char* path) noexcept;
int last_error() noexcept;

enum class State { idle, waiting, ready, failed };
enum class Failure { none, invalid_pid, cleanup, open, permission, write, sync, close, publish, observe, timeout };

class Broker final {
public:
    State state = State::idle;
    Failure failure = Failure::none;
    int pid = -1, uid_before = -1, uid_after = -1;
    int cleanup_result = 0, open_result = 0, permission_result = 0;
    int write_result = 0, sync_result = 0, close_result = 0, publish_result = 0;
    int exists_result = 0, error = 0;
    std::size_t polls = 0, post_consume_polls = 0, request_size = 0;
    bool request_observed_missing = false;
    std::array<char, 32> request{};

    bool start() noexcept;
    void step() noexcept;
    bool consumed() const noexcept { return state == State::ready; }
    std::string_view summary() const noexcept;
};
}
