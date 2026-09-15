// etaHEN/OnionHEN app-jailbreak request probe. SPDX-License-Identifier: GPL-3.0-or-later
#include "privilege_request.hpp"

namespace privilege_probe {
namespace {
bool append(std::array<char, 32>& target, std::size_t& at, char value) noexcept {
    if (at + 1 >= target.size()) return false;
    target[at++] = value; target[at] = 0; return true;
}
bool append_text(std::array<char, 32>& target, std::size_t& at, const char* value) noexcept {
    while (*value) if (!append(target, at, *value++)) return false;
    return true;
}
bool append_decimal(std::array<char, 32>& target, std::size_t& at, unsigned value) noexcept {
    std::array<char, 12> reversed{}; std::size_t count = 0;
    do { reversed[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    while (count) if (!append(target, at, reversed[--count])) return false;
    return true;
}
}

bool Broker::start() noexcept {
    *this = {};
    pid = process_id(); uid_before = effective_user_id(); uid_after = uid_before;
    if (pid <= 1) { state = State::failed; failure = Failure::invalid_pid; return false; }
    // A relaunch can find the native app already running as root (for
    // example, after a previous OnionHEN request). Do not publish a second
    // request in that case; launch only needs the credential check.
    if (uid_before == 0) {
        state = State::ready;
        return true;
    }
    std::size_t at = 0;
    if (!append_text(request, at, "{\"PID\":") ||
        !append_decimal(request, at, static_cast<unsigned>(pid)) ||
        !append_text(request, at, "}\n")) {
        state = State::failed; failure = Failure::invalid_pid; return false;
    }
    request_size = at;
    cleanup_result = remove_request(request_path);
    if (cleanup_result != 0) { error = last_error(); state = State::failed; failure = Failure::cleanup; return false; }
    cleanup_result = remove_request(staged_request_path);
    if (cleanup_result != 0) { error = last_error(); state = State::failed; failure = Failure::cleanup; return false; }
    open_result = open_request(staged_request_path);
    if (open_result < 0) { error = last_error(); state = State::failed; failure = Failure::open; return false; }
    permission_result = make_request_readable(open_result);
    if (permission_result != 0) {
        error = last_error(); close_result = close_request(open_result); remove_request(staged_request_path);
        state = State::failed; failure = Failure::permission; return false;
    }
    std::size_t written = 0;
    while (written < request_size) {
        write_result = write_request(open_result, request.data() + written, request_size - written);
        if (write_result <= 0 || static_cast<std::size_t>(write_result) > request_size - written) {
            error = last_error(); close_result = close_request(open_result); remove_request(staged_request_path);
            state = State::failed; failure = Failure::write; return false;
        }
        written += static_cast<std::size_t>(write_result);
    }
    sync_result = sync_request(open_result);
    if (sync_result != 0) {
        error = last_error(); close_result = close_request(open_result); remove_request(staged_request_path);
        state = State::failed; failure = Failure::sync; return false;
    }
    close_result = close_request(open_result);
    if (close_result != 0) {
        error = last_error(); remove_request(staged_request_path);
        state = State::failed; failure = Failure::close; return false;
    }
    publish_result = publish_request(staged_request_path, request_path);
    if (publish_result != 0) {
        error = last_error(); remove_request(staged_request_path);
        state = State::failed; failure = Failure::publish; return false;
    }
    state = State::waiting; return true;
}

void Broker::step() noexcept {
    if (state != State::waiting) return;
    ++polls; exists_result = request_exists(request_path);
    if (!request_observed_missing && exists_result == 0) {
        if (polls >= max_polls) { error = last_error(); state = State::failed; failure = Failure::timeout; }
        return;
    }
    // The request disappearing is only the consume signal. The HEN daemon then
    // needs a short, console-dependent interval to finish the process
    // jailbreak. Launching during that interval makes SceLncService reject
    // the call with 0x8094000F (isSystemProcess == false).
    if (!request_observed_missing) {
        error = last_error();
        request_observed_missing = true;
        post_consume_polls = 0;
    }
    ++post_consume_polls;
    uid_after = effective_user_id();
    if (uid_after == 0 || post_consume_polls >= post_consume_max_polls)
        state = State::ready;
}

std::string_view Broker::summary() const noexcept {
    if (state == State::idle) return "HEN REQUEST NOT SENT";
    if (state == State::waiting) {
        return request_observed_missing ?
            "HEN CONSUMED - WAITING FOR FULL JAILBREAK" :
            "WAITING FOR HEN TO CONSUME REQUEST";
    }
    if (state == State::ready)
        return uid_before == 0 ? "ALREADY PRIVILEGED - LAUNCH RETRY READY" :
            "HEN CONSUMED REQUEST - LAUNCH RETRY READY";
    switch (failure) {
    case Failure::invalid_pid: return "INVALID PROCESS ID - REQUEST NOT SENT";
    case Failure::cleanup: return "STALE HEN REQUEST CLEANUP FAILED";
    case Failure::open: return "HEN REQUEST OPEN FAILED";
    case Failure::permission: return "REQUEST MODE 0666 APPLY FAILED";
    case Failure::write: return "HEN REQUEST WRITE FAILED";
    case Failure::sync: return "HEN REQUEST SYNC FAILED";
    case Failure::close: return "HEN REQUEST CLOSE FAILED";
    case Failure::publish: return "ATOMIC HEN REQUEST PUBLISH FAILED";
    case Failure::observe: return "HEN REQUEST STATUS CHECK FAILED";
    case Failure::timeout: return "HEN DID NOT CONSUME REQUEST - CHECK ALLOWLIST";
    default: return "HEN REQUEST FAILED";
    }
}
}
