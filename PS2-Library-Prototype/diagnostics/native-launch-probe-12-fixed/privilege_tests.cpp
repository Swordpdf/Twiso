// Host tests for OnionHEN request state machine. SPDX-License-Identifier: GPL-3.0-or-later
#include "privilege_request.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

namespace privilege_probe {
namespace {
int fake_pid = 104, fake_uid = 100, fake_cleanup = 0, fake_open = 7, fake_permission = 0;
int fake_write = 0, fake_sync = 0, fake_close = 0, fake_publish = 0;
int fake_exists = 0, fake_errno = 0, cleanup_calls = 0, permission_calls = 0, publish_calls = 0;
char captured[64]{}; std::size_t captured_size = 0;
}
int process_id() noexcept { return fake_pid; }
int effective_user_id() noexcept { return fake_uid; }
int remove_request(const char*) noexcept { ++cleanup_calls; return fake_cleanup; }
int open_request(const char* path) noexcept { assert(std::string_view(path) == staged_request_path); return fake_open; }
int make_request_readable(int descriptor) noexcept { assert(descriptor == fake_open); ++permission_calls; return fake_permission; }
int write_request(int, const void* data, std::size_t size) noexcept {
    const auto amount = fake_write > 0 && static_cast<std::size_t>(fake_write) < size ? static_cast<std::size_t>(fake_write) : size;
    std::memcpy(captured + captured_size, data, amount); captured_size += amount; return static_cast<int>(amount);
}
int sync_request(int) noexcept { return fake_sync; }
int close_request(int) noexcept { return fake_close; }
int publish_request(const char* source, const char* target) noexcept {
    assert(std::string_view(source) == staged_request_path && std::string_view(target) == request_path);
    ++publish_calls; return fake_publish;
}
int request_exists(const char*) noexcept { return fake_exists; }
int last_error() noexcept { return fake_errno; }
}

int main() {
    using namespace privilege_probe;
    Broker broker;
    assert(broker.start());
    assert(std::string_view(captured, captured_size) == "{\"PID\":104}\n");
    assert(cleanup_calls == 2 && permission_calls == 1 && publish_calls == 1);
    assert(broker.permission_result == 0 && broker.publish_result == 0);
    assert(broker.state == State::waiting && broker.uid_before == 100);
    broker.step(); assert(broker.state == State::waiting);
    // The request disappearing starts the bounded full-jailbreak grace
    // period even if the UID probe still reports the pre-request value.
    fake_exists = -1; fake_errno = 13;
    for (std::size_t i = 0; i < post_consume_max_polls && broker.state == State::waiting; ++i)
        broker.step();
    assert(broker.state == State::ready && broker.uid_after == 100 && broker.request_observed_missing);

    fake_pid = 1; Broker invalid; assert(!invalid.start() && invalid.failure == Failure::invalid_pid);
    fake_uid = 100; fake_pid = 105; fake_open = -1; fake_errno = 13; Broker denied;
    assert(!denied.start() && denied.failure == Failure::open && denied.error == 13);
    fake_open = 8; fake_permission = -1; fake_errno = 1; Broker mode_denied;
    assert(!mode_denied.start() && mode_denied.failure == Failure::permission && mode_denied.error == 1);
    fake_permission = 0; fake_publish = -1; fake_errno = 13; Broker publish_denied;
    assert(!publish_denied.start() && publish_denied.failure == Failure::publish && publish_denied.error == 13);
    fake_publish = 0; fake_exists = 0; fake_uid = 100; fake_errno = 0; Broker timeout; assert(timeout.start());
    for (std::size_t i = 0; i < max_polls; ++i) timeout.step();
    assert(timeout.failure == Failure::timeout);
    std::cout << "Passed 25 host checks: stale cleanup, staged 0666 request, atomic publish, exact PID JSON, consume detection, UID observation, and timeout.\n";
}
