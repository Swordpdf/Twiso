// SPDX-License-Identifier: GPL-3.0-or-later
#include "iso_directory.hpp"

namespace iso_probe {
namespace {
char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
}

bool iso_name(std::string_view name) noexcept {
    if (name.size() <= 4 || name.size() > 255) return false;
    for (char c : name) if (c == '\0' || c == '/') return false;
    const std::string_view tail{name.data() + name.size() - 4, 4};
    return tail[0] == '.' && lower(tail[1]) == 'i' && lower(tail[2]) == 's' && lower(tail[3]) == 'o';
}

void display_name(std::string_view name, std::span<char> output) noexcept {
    if (output.empty()) return;
    const auto n = name.size() < output.size() - 1 ? name.size() : output.size() - 1;
    // The UI now ships full printable-ASCII glyphs, so keep every 0x20..0x7E
    // character (commas, ampersands, brackets, ...) instead of collapsing it.
    for (std::size_t i = 0; i < n; ++i) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
        const auto code = static_cast<unsigned char>(c);
        output[i] = (code >= 0x20 && code <= 0x7E) ? c : '?';
    }
    if (n < name.size() && n >= 3)
        for (std::size_t i = n - 3; i < n; ++i) output[i] = '.';
    output[n] = '\0';
}

bool Directory::busy() const noexcept {
    return state == State::opening || state == State::reading || state == State::closing;
}

bool Directory::request() noexcept {
    if (busy() || descriptor_ >= 0) return false;
    count = records = batches = unknown_iso_types = 0;
    open_result = read_result = close_result = 0;
    open_called = read_called = close_called = invalid_data = false;
    state = State::opening;
    return true;
}

Parse Directory::consume(std::span<const unsigned char> data) noexcept {
    // FreeBSD 11/PS5 dirent wire layout: u32 inode, u16 record size,
    // u8 type, u8 name length, name + NUL. No casts of unaligned records.
    std::size_t at = 0;
    while (at < data.size()) {
        const auto remaining = data.size() - at;
        if (remaining < 8) return Parse::invalid;
        const auto* p = data.data() + at;
        const std::size_t size = p[4] | (static_cast<std::size_t>(p[5]) << 8);
        const std::size_t length = p[7];
        if (size < 12 || size % 4 != 0 || size > remaining || 8 + length >= size)
            return Parse::invalid;
        if (p[8 + length] != 0) return Parse::invalid;
        const std::string_view name{reinterpret_cast<const char*>(p + 8), length};
        for (char c : name) if (c == '\0' || c == '/') return Parse::invalid;
        if (records == max_records) return Parse::limit;
        ++records;
        const bool allocated = p[0] || p[1] || p[2] || p[3];
        if (allocated && iso_name(name)) {
            if (p[6] == 0) ++unknown_iso_types;
            if (p[6] == 8) { // DT_REG only; directories, symlinks and unknown types excluded.
                bool duplicate = false;
                for (std::size_t i = 0; i < count; ++i)
                    if (names[i].view() == name) { duplicate = true; break; }
                if (!duplicate) {
                    if (count == capacity) return Parse::limit;
                    auto& target = names[count++];
                    target.length = length;
                    for (std::size_t i = 0; i < length; ++i) target.bytes[i] = name[i];
                    target.bytes[length] = '\0';
                }
            }
        }
        at += size;
    }
    return Parse::ok;
}

void Directory::sort() noexcept {
    // Bounded, allocation-free insertion sort; exact byte names remain intact.
    for (std::size_t i = 1; i < count; ++i) {
        Name current = names[i];
        std::size_t j = i;
        while (j > 0 && current.view() < names[j - 1].view()) {
            names[j] = names[j - 1]; --j;
        }
        names[j] = current;
    }
}

void Directory::finish(State result) noexcept {
    after_close_ = result;
    if (result == State::failed) count = 0; // Never present a failed scan as a complete list.
    state = State::closing;
}

void Directory::step() noexcept {
    switch (state) {
    case State::opening:
        open_called = true;
        open_result = open_directory();
        if (open_result < 0) { state = State::failed; return; }
        descriptor_ = open_result;
        state = State::reading;
        return;
    case State::reading: {
        if (batches == max_batches) { finish(State::limited); return; }
        ++batches;
        read_called = true;
        read_result = read_directory(descriptor_, buffer_.data(), static_cast<int>(buffer_.size()));
        if (read_result < 0) { finish(State::failed); return; }
        if (read_result == 0) { finish(State::done); return; }
        if (read_result > static_cast<int>(buffer_.size())) {
            invalid_data = true; finish(State::failed); return;
        }
        const auto result = consume({reinterpret_cast<const unsigned char*>(buffer_.data()),
                                     static_cast<std::size_t>(read_result)});
        if (result == Parse::invalid) { invalid_data = true; finish(State::failed); }
        if (result == Parse::limit) finish(State::limited);
        return;
    }
    case State::closing:
        close_called = true;
        close_result = close_directory(descriptor_);
        if (close_result != 0) {
            // Don't retry an ambiguous close or reopen more descriptors.
            count = 0; state = State::failed; return;
        }
        descriptor_ = -1;
        sort();
        state = after_close_;
        return;
    default:
        return;
    }
}

std::string_view Directory::summary() const noexcept {
    switch (state) {
    case State::idle: return "TRIANGLE TO SCAN - NO DIRECTORY ACCESS YET";
    case State::opening: return "NEXT - OPEN ISO DIRECTORY READ ONLY";
    case State::reading: return "READING DIRECTORY NAMES - NO ISO CONTENT READ";
    case State::closing: return "NEXT - CLOSE DIRECTORY";
    case State::done: return count ? "SCAN COMPLETE - FILENAMES ONLY" : "SCAN COMPLETE - NO REGULAR ISO FILES FOUND";
    case State::limited: return "SCAN LIMIT REACHED - LIST IS PARTIAL";
    case State::failed:
        if (open_result < 0) return "DIRECTORY OPEN FAILED - SEE RAW RESULT BELOW";
        if (close_result != 0) return "DIRECTORY CLOSE FAILED - CLOSE APP BEFORE RETRY";
        if (invalid_data) return "INVALID DIRECTORY DATA - SCAN STOPPED";
        return "DIRECTORY READ FAILED - SEE RAW RESULT BELOW";
    }
    return "UNKNOWN STATE";
}
} // namespace iso_probe
