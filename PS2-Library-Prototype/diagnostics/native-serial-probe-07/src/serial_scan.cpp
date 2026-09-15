// SPDX-License-Identifier: GPL-3.0-or-later
#include "serial_scan.hpp"

namespace serial_probe {
namespace {
std::uint32_t le32(const unsigned char* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
        (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
bool equal(const unsigned char* p, std::string_view value) noexcept {
    for (std::size_t i = 0; i < value.size(); ++i)
        if (p[i] != static_cast<unsigned char>(value[i])) return false;
    return true;
}
bool upper(char c) noexcept { return c >= 'A' && c <= 'Z'; }
bool digit(char c) noexcept { return c >= '0' && c <= '9'; }
char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
bool iso_name(std::string_view name) noexcept {
    if (name.size() <= 4 || name.size() > 255) return false;
    for (char c : name) if (c == '\0' || c == '/') return false;
    const auto at = name.size() - 4;
    return name[at] == '.' && lower(name[at + 1]) == 'i' &&
        lower(name[at + 2]) == 's' && lower(name[at + 3]) == 'o';
}
}

bool Scanner::busy() const noexcept {
    return state >= State::opening && state <= State::closing;
}

bool Scanner::clear() noexcept {
    if (busy() || descriptor_ >= 0) return false;
    state = State::idle; failure = Failure::none; serial.fill(0);
    operations = 0; open_result = read_result = close_result = error = 0;
    last_offset = 0;
    open_called = seek_called = read_called = close_called = false;
    return true;
}

bool Scanner::request(std::string_view filename) noexcept {
    if (busy() || descriptor_ >= 0) return false;
    clear(); path.fill(0);
    if (!iso_name(filename)) {
        state = State::failed; failure = Failure::invalid_name; return false;
    }
    constexpr std::string_view prefix = "/data/PS2/isos/";
    if (prefix.size() + filename.size() + 1 > path.size()) {
        state = State::failed; failure = Failure::path_too_long; return false;
    }
    std::size_t at = 0;
    for (char c : prefix) path[at++] = c;
    for (char c : filename) path[at++] = c;
    path[at] = '\0';
    pvd_sector_ = 16; root_offset_ = cnf_offset_ = 0;
    root_size_ = root_done_ = cnf_size_ = 0; wanted_ = filled_ = 0;
    state = State::opening;
    return true;
}

void Scanner::finish(State result) noexcept {
    after_close_ = result;
    state = descriptor_ >= 0 ? State::closing : result;
}

void Scanner::fail(Failure reason) noexcept {
    failure = reason; serial.fill(0); finish(State::failed);
}

bool Scanner::begin_seek(State next, std::uint64_t offset, std::size_t wanted) noexcept {
    if (operations == max_operations) { fail(Failure::operation_limit); return false; }
    ++operations; seek_called = true; last_offset = offset;
    const auto result = seek_iso(descriptor_, offset);
    if (result < 0 || static_cast<std::uint64_t>(result) != offset) {
        error = result < 0 ? last_error() : 0; fail(Failure::seek); return false;
    }
    wanted_ = wanted; filled_ = 0; state = next; return true;
}

bool Scanner::take_read() noexcept {
    if (operations == max_operations) { fail(Failure::operation_limit); return false; }
    ++operations; read_called = true;
    const auto remaining = wanted_ - filled_;
    read_result = read_iso(descriptor_, buffer_.data() + filled_, remaining);
    if (read_result < 0) { error = last_error(); fail(Failure::read); return false; }
    if (read_result == 0 || static_cast<std::size_t>(read_result) > remaining) {
        error = 0; fail(Failure::read); return false;
    }
    filled_ += static_cast<std::size_t>(read_result);
    return filled_ == wanted_;
}

void Scanner::parse_pvd() noexcept {
    if (!equal(buffer_.data() + 1, "CD001")) { fail(Failure::invalid_iso); return; }
    if (buffer_[0] == 255) { fail(Failure::invalid_iso); return; }
    if (buffer_[0] != 1) {
        if (++pvd_sector_ == 48) { fail(Failure::invalid_iso); return; }
        state = State::seeking_pvd; return;
    }
    if (buffer_[128] != 0 || buffer_[129] != 8 || buffer_[156] < 34) {
        fail(Failure::invalid_iso); return;
    }
    root_offset_ = std::uint64_t(le32(buffer_.data() + 158)) * sector_size;
    root_size_ = le32(buffer_.data() + 166);
    if (!root_size_ || root_size_ > max_root_size) { fail(Failure::invalid_iso); return; }
    root_done_ = 0; state = State::seeking_root;
}

void Scanner::parse_root() noexcept {
    for (std::size_t at = 0; at < wanted_;) {
        const unsigned size = buffer_[at];
        if (size == 0) break; // ISO9660 sector padding.
        if (size < 34 || at + size > wanted_) { fail(Failure::invalid_iso); return; }
        const unsigned name_size = buffer_[at + 32];
        if (33U + name_size > size) { fail(Failure::invalid_iso); return; }
        const auto* name = buffer_.data() + at + 33;
        const bool system = (name_size == 12 && equal(name, "SYSTEM.CNF;1")) ||
                            (name_size == 10 && equal(name, "SYSTEM.CNF"));
        if (!(buffer_[at + 25] & 2) && system) {
            cnf_offset_ = std::uint64_t(le32(buffer_.data() + at + 2)) * sector_size;
            cnf_size_ = le32(buffer_.data() + at + 10);
            if (!cnf_size_ || cnf_size_ > max_cnf_size) {
                fail(Failure::invalid_system_cnf); return;
            }
            state = State::seeking_cnf; return;
        }
        at += size;
    }
    root_done_ += static_cast<std::uint32_t>(wanted_);
    if (root_done_ >= root_size_) { fail(Failure::system_cnf_missing); return; }
    state = State::seeking_root;
}

void Scanner::parse_cnf() noexcept {
    buffer_[cnf_size_] = 0;
    for (std::size_t i = 0; i < cnf_size_; ++i)
        if (buffer_[i] == 0) { fail(Failure::invalid_system_cnf); return; }
    for (std::size_t line = 0; line + 5 <= cnf_size_;) {
        std::size_t end = line;
        while (end < cnf_size_ && buffer_[end] != '\n' && buffer_[end] != '\r') ++end;
        bool boot2 = false;
        for (std::size_t i = line; i + 5 <= end; ++i)
            if (equal(buffer_.data() + i, "BOOT2")) { boot2 = true; break; }
        if (boot2) {
            for (std::size_t i = line; i + 11 <= end; ++i) {
                const char* p = reinterpret_cast<const char*>(buffer_.data() + i);
                if (p[0] == 'S' && upper(p[1]) && upper(p[2]) && upper(p[3]) &&
                    p[4] == '_' && digit(p[5]) && digit(p[6]) && digit(p[7]) &&
                    p[8] == '.' && digit(p[9]) && digit(p[10])) {
                    for (unsigned j = 0; j < 4; ++j) serial[j] = p[j];
                    serial[4] = '-';
                    for (unsigned j = 0; j < 3; ++j) serial[5 + j] = p[5 + j];
                    serial[8] = p[9]; serial[9] = p[10]; serial[10] = 0;
                    finish(State::done); return;
                }
            }
        }
        line = end;
        while (line < cnf_size_ && (buffer_[line] == '\n' || buffer_[line] == '\r')) ++line;
    }
    fail(Failure::serial_missing);
}

void Scanner::step() noexcept {
    switch (state) {
    case State::opening:
        if (operations == max_operations) { fail(Failure::operation_limit); return; }
        ++operations; open_called = true; open_result = open_iso(path.data());
        if (open_result < 0) { error = last_error(); descriptor_ = -1; fail(Failure::open); return; }
        descriptor_ = open_result; state = State::seeking_pvd; return;
    case State::seeking_pvd:
        begin_seek(State::reading_pvd, std::uint64_t(pvd_sector_) * sector_size, sector_size); return;
    case State::reading_pvd:
        if (take_read()) parse_pvd();
        return;
    case State::seeking_root: {
        const auto left = static_cast<std::size_t>(root_size_ - root_done_);
        const auto amount = left < sector_size ? left : sector_size;
        begin_seek(State::reading_root, root_offset_ + root_done_, amount); return;
    }
    case State::reading_root:
        if (take_read()) parse_root();
        return;
    case State::seeking_cnf:
        begin_seek(State::reading_cnf, cnf_offset_, cnf_size_); return;
    case State::reading_cnf:
        if (take_read()) parse_cnf();
        return;
    case State::closing:
        close_called = true; ++operations; close_result = close_iso(descriptor_);
        if (close_result != 0) {
            error = last_error(); failure = Failure::close; serial.fill(0);
            state = State::failed; return; // Do not retry an ambiguous close.
        }
        descriptor_ = -1; state = after_close_; return;
    default: return;
    }
}

std::string_view Scanner::summary() const noexcept {
    if (state == State::idle) return "CROSS IDENTIFIES SELECTED ISO - NOT READ YET";
    if (busy()) return "READING SELECTED ISO METADATA - PLEASE WAIT";
    if (state == State::done) return "GAME ID READ FROM SYSTEM.CNF";
    switch (failure) {
    case Failure::invalid_name: return "SELECTED ISO NAME IS INVALID";
    case Failure::path_too_long: return "SELECTED ISO PATH IS TOO LONG";
    case Failure::open: return "SELECTED ISO OPEN FAILED";
    case Failure::seek: return "SELECTED ISO SEEK FAILED";
    case Failure::read: return "SELECTED ISO READ FAILED";
    case Failure::invalid_iso: return "ISO9660 METADATA IS INVALID OR UNSUPPORTED";
    case Failure::system_cnf_missing: return "SYSTEM.CNF NOT FOUND IN ROOT DIRECTORY";
    case Failure::invalid_system_cnf: return "SYSTEM.CNF IS INVALID OR TOO LARGE";
    case Failure::serial_missing: return "PS2 GAME ID NOT FOUND IN BOOT2 LINE";
    case Failure::close: return "SELECTED ISO CLOSE FAILED - CLOSE APP";
    case Failure::operation_limit: return "ISO METADATA OPERATION LIMIT REACHED";
    default: return "IDENTIFICATION FAILED";
    }
}
} // namespace serial_probe
