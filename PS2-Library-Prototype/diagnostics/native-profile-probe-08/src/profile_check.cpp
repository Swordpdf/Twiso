// SPDX-License-Identifier: GPL-3.0-or-later
#include "profile_check.hpp"

namespace profile_probe {
namespace {
bool equal(std::string_view a, std::string_view b) noexcept { return a == b; }
bool digit(char c) noexcept { return c >= '0' && c <= '9'; }
bool upper(char c) noexcept { return c >= 'A' && c <= 'Z'; }
bool lower(char c) noexcept { return c >= 'a' && c <= 'z'; }
bool serial_valid(std::string_view s) noexcept {
    if (s.size() != 10 || s[4] != '-') return false;
    for (unsigned i = 0; i < 4; ++i) if (!upper(s[i])) return false;
    for (unsigned i = 5; i < 10; ++i) if (!digit(s[i])) return false;
    return true;
}
bool copy(std::string_view value, char* target, std::size_t capacity) noexcept {
    if (value.empty() || value.size() >= capacity) return false;
    for (std::size_t i = 0; i < value.size(); ++i) target[i] = value[i];
    target[value.size()] = 0; return true;
}
bool safe_ascii(std::string_view value) noexcept {
    for (const unsigned char c : value)
        if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c > 126) return false;
    return true;
}
std::size_t find_char(std::string_view value, char wanted) noexcept {
    for (std::size_t i = 0; i < value.size(); ++i) if (value[i] == wanted) return i;
    return std::string_view::npos;
}
bool contains(std::string_view value, std::string_view wanted) noexcept {
    if (wanted.empty() || wanted.size() > value.size()) return false;
    for (std::size_t i = 0; i + wanted.size() <= value.size(); ++i) {
        bool same = true;
        for (std::size_t j = 0; j < wanted.size(); ++j) if (value[i + j] != wanted[j]) { same = false; break; }
        if (same) return true;
    }
    return false;
}
bool safe_path(std::string_view value, std::string_view prefix, std::string_view suffix) noexcept {
    if (!value.starts_with(prefix) || !value.ends_with(suffix) || value.size() >= 257) return false;
    if (contains(value, "..") || contains(value, "//")) return false;
    for (char c : value) if (c == '\0' || c == '\\' || c == '|') return false;
    return true;
}
bool safe_iso_path(std::string_view value) noexcept {
    constexpr std::string_view prefix = "/data/PS2/isos/";
    if (!value.starts_with(prefix) || value.size() <= prefix.size() + 4 ||
        contains(value, "..") || contains(value, "//")) return false;
    const auto extension = value.substr(value.size() - 4);
    if (extension[0] != '.' || (extension[1] != 'i' && extension[1] != 'I') ||
        (extension[2] != 's' && extension[2] != 'S') ||
        (extension[3] != 'o' && extension[3] != 'O')) return false;
    for (char c : value)
        if (c == '\0' || c == '\\' || c == '|' || c == '"' ||
            static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126)
            return false;
    return true;
}
bool external_vmc_path(std::string_view value, const Profile& profile) noexcept {
    constexpr std::string_view prefix = "/data/PS2/saves/";
    constexpr std::size_t serial_size = 10;
    if (!value.starts_with(prefix) || value.size() != prefix.size() + serial_size ||
        !serial_valid(std::string_view{profile.serial.data(), serial_size})) return false;
    return value.substr(prefix.size()) == std::string_view{profile.serial.data(), serial_size};
}
bool id(std::string_view value) noexcept {
    if (value.empty() || value.size() >= 33) return false;
    for (char c : value) if (!(lower(c) || digit(c) || c == '-')) return false;
    return true;
}
bool launcher(std::string_view value) noexcept {
    if (value.size() != 9) return false;
    for (char c : value) if (!(upper(c) || digit(c))) return false;
    return true;
}
bool number(std::string_view value, std::uint64_t& output) noexcept {
    if (value.empty() || value.size() > 20) return false;
    std::uint64_t result = 0;
    for (char c : value) {
        if (!digit(c)) return false;
        const auto next = result * 10 + static_cast<unsigned>(c - '0');
        if (next < result) return false;
        result = next;
    }
    output = result; return true;
}
std::string_view unquote(std::string_view value) noexcept {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.remove_prefix(1); value.remove_suffix(1);
    }
    return value;
}
bool append(char* target, std::size_t capacity, std::size_t& at, std::string_view value) noexcept {
    if (at + value.size() + 1 > capacity) return false;
    for (char c : value) target[at++] = c;
    target[at] = 0; return true;
}
}

Parse parse_profile(std::string_view text, std::string_view expected_serial,
                    Profile& output) noexcept {
    output = {};
    if (!serial_valid(expected_serial) || text.empty() || text.size() > max_profile_size || !safe_ascii(text))
        return Parse::invalid;
    unsigned seen = 0;
    while (!text.empty()) {
        const auto end = find_char(text, '\n');
        auto line = std::string_view{text.data(), end == std::string_view::npos ? text.size() : end};
        text = end == std::string_view::npos ? std::string_view{} :
            std::string_view{text.data() + end + 1, text.size() - end - 1};
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto at = find_char(line, '=');
        if (at == std::string_view::npos || at == 0 || at + 1 == line.size()) return Parse::invalid;
        const auto key = std::string_view{line.data(), at};
        const auto value = std::string_view{line.data() + at + 1, line.size() - at - 1};
        unsigned bit = 0;
        if (key == "schema") {
            bit = 1U << 0; if (value != "1") return Parse::invalid;
        } else if (key == "serial") {
            bit = 1U << 1; if (!serial_valid(value) || !copy(value, output.serial.data(), output.serial.size())) return Parse::invalid;
        } else if (key == "name") {
            bit = 1U << 2; if (!copy(value, output.name.data(), output.name.size())) return Parse::invalid;
        } else if (key == "status") {
            bit = 1U << 3;
            if (value == "console-tested") output.status = Status::console_tested;
            else if (value == "review-required") output.status = Status::review_required;
            else if (value == "as-is") output.status = Status::as_is;
            else return Parse::invalid;
        } else if (key == "backend") {
            bit = 1U << 4; if (!id(value) || !copy(value, output.backend.data(), output.backend.size())) return Parse::invalid;
        } else if (key == "launcher") {
            bit = 1U << 5; if (!launcher(value) || !copy(value, output.launcher.data(), output.launcher.size())) return Parse::invalid;
        } else if (key == "config") {
            bit = 1U << 6;
            if (!safe_path(value, "/data/PS2/library/profiles/", ".txt") || !copy(value, output.config.data(), output.config.size())) return Parse::invalid;
        } else if (key == "lua") {
            bit = 1U << 7;
            if (value == "-") { if (!copy(value, output.lua.data(), output.lua.size())) return Parse::invalid; }
            else if (!safe_path(value, "/data/PS2/configs/", ".lua") || !copy(value, output.lua.data(), output.lua.size())) return Parse::invalid;
        } else if (key == "size") {
            bit = 1U << 8; if (!number(value, output.expected_size)) return Parse::invalid;
        } else if (key == "source") {
            bit = 1U << 9; // Informational but bounded and ASCII-checked with the whole file.
            if (value.size() > 160) return Parse::invalid;
        } else return Parse::invalid;
        if (seen & bit) return Parse::invalid;
        seen |= bit;
    }
    constexpr unsigned base_required = (1U << 0) | (1U << 1) | (1U << 2) | (1U << 3);
    if ((seen & base_required) != base_required) return Parse::invalid;
    if (!equal(output.serial.data(), expected_serial)) return Parse::mismatch;
    if (output.status == Status::review_required) return Parse::review;
    constexpr unsigned tested_required = (1U << 9) - 1;
    if ((seen & tested_required) != tested_required) return Parse::invalid;
    if (output.status == Status::as_is) return Parse::ok;
    return output.status == Status::console_tested && output.expected_size != 0 ? Parse::ok : Parse::invalid;
}

bool validate_config(std::string_view text, const Profile& profile,
                     std::string_view iso_path) noexcept {
    if (text.empty() || text.size() > max_text_size || !safe_ascii(text)) return false;
    unsigned required = 0;
    while (!text.empty()) {
        const auto end = find_char(text, '\n');
        auto line = std::string_view{text.data(), end == std::string_view::npos ? text.size() : end};
        text = end == std::string_view::npos ? std::string_view{} :
            std::string_view{text.data() + end + 1, text.size() - end - 1};
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto at = find_char(line, '=');
        if (!line.starts_with("--") || at < 3 || at == std::string_view::npos) return false;
        const auto key = std::string_view{line.data(), at};
        const auto value = unquote({line.data() + at + 1, line.size() - at - 1});
        unsigned bit = 0;
        if (key == "--ps2-title-id") { bit = 1U << 0; if (value != profile.serial.data()) return false; }
        else if (key == "--image") {
            bit = 1U << 1;
            // An as-is profile is deliberately reusable across filenames. The
            // apply transaction rewrites this option to the selected ISO path.
            if (profile.status == Status::as_is) {
                if (!safe_iso_path(value)) return false;
            } else if (value != iso_path) return false;
        }
        else if (key == "--config-local-lua") { bit = 1U << 2; if (profile.lua[0] == '-' || value != profile.lua.data()) return false; }
        else if (key == "--path-vmc") {
            bit = 1U << 3;
            // The maker's default remains valid.  The shared-VMC build may
            // rewrite it to the selected disc serial's external directory.
            if (value != "/tmp/vmc" && !external_vmc_path(value, profile)) return false;
        }
        else if (key == "--rom") { bit = 1U << 4; if (value != "PS20220WD20050620.crack") return false; }
        else if (key == "--max-disc-num") { bit = 1U << 5; if (value != "1") return false; }
        else if (key == "--config" || key == "--path-patches" || key == "--path-featuredata" ||
                 key == "--load-feature-lua" || key == "--load-tooling-lua" || key.starts_with("--image-disc")) return false;
        if (bit) { if (required & bit) return false; required |= bit; }
    }
    const unsigned expected = profile.lua[0] == '-' ? 0x3bU : 0x3fU;
    return required == expected;
}

bool rewrite_as_is_image(char* text, std::size_t capacity, std::size_t& size,
                         std::string_view iso_path) noexcept {
    if (text == nullptr || size >= capacity || !safe_iso_path(iso_path)) return false;
    constexpr std::string_view prefix = "--image=\"";
    constexpr std::string_view suffix = "\"";
    std::size_t image_start = std::string_view::npos;
    std::size_t image_end = std::string_view::npos;
    std::size_t at = 0;
    while (at < size) {
        const auto newline = find_char({text + at, size - at}, '\n');
        const auto end = newline == std::string_view::npos ? size : at + newline;
        auto line_end = end;
        if (line_end > at && text[line_end - 1] == '\r') --line_end;
        const std::string_view line{text + at, line_end - at};
        if (line.starts_with("--image=")) {
            if (image_start != std::string_view::npos) return false;
            image_start = at; image_end = line_end;
        }
        if (newline == std::string_view::npos) break;
        at = end + 1;
    }
    if (image_start == std::string_view::npos || image_end < image_start) return false;
    const auto replacement_size = prefix.size() + iso_path.size() + suffix.size();
    const auto old_size = image_end - image_start;
    const auto new_size = size - old_size + replacement_size;
    if (new_size >= capacity) return false;
    if (replacement_size > old_size) {
        for (std::size_t i = size - image_end; i > 0; --i)
            text[image_end + (replacement_size - old_size) + i - 1] = text[image_end + i - 1];
    } else if (replacement_size < old_size) {
        for (std::size_t i = 0; i < size - image_end; ++i)
            text[image_start + replacement_size + i] = text[image_end + i];
    }
    std::size_t write = image_start;
    for (char c : prefix) text[write++] = c;
    for (char c : iso_path) text[write++] = c;
    for (char c : suffix) text[write++] = c;
    size = new_size; text[size] = 0;
    return true;
}

bool rewrite_vmc_path(char* text, std::size_t capacity, std::size_t& size,
                      std::string_view serial) noexcept {
    if (text == nullptr || size >= capacity || !serial_valid(serial)) return false;
    constexpr std::string_view prefix = "--path-vmc=\"/data/PS2/saves/";
    constexpr std::string_view suffix = "\"";
    std::size_t line_start = std::string_view::npos;
    std::size_t line_end = std::string_view::npos;
    std::size_t at = 0;
    while (at < size) {
        const auto newline = find_char({text + at, size - at}, '\n');
        const auto end = newline == std::string_view::npos ? size : at + newline;
        auto content_end = end;
        if (content_end > at && text[content_end - 1] == '\r') --content_end;
        const std::string_view line{text + at, content_end - at};
        if (line.starts_with("--path-vmc=")) {
            if (line_start != std::string_view::npos) return false;
            line_start = at; line_end = content_end;
        }
        if (newline == std::string_view::npos) break;
        at = end + 1;
    }
    if (line_start == std::string_view::npos || line_end < line_start) return false;
    const auto replacement_size = prefix.size() + serial.size() + suffix.size();
    const auto old_size = line_end - line_start;
    const auto new_size = size - old_size + replacement_size;
    if (new_size >= capacity) return false;
    if (replacement_size > old_size) {
        for (std::size_t i = size - line_end; i > 0; --i)
            text[line_end + (replacement_size - old_size) + i - 1] = text[line_end + i - 1];
    } else if (replacement_size < old_size) {
        for (std::size_t i = 0; i < size - line_end; ++i)
            text[line_start + replacement_size + i] = text[line_end + i];
    }
    std::size_t write = line_start;
    for (char c : prefix) text[write++] = c;
    for (char c : serial) text[write++] = c;
    for (char c : suffix) text[write++] = c;
    size = new_size; text[size] = 0;
    return true;
}

bool validate_lua(std::string_view text) noexcept {
    if (text.empty() || text.size() > max_text_size) return false;
    bool content = false;
    for (const unsigned char c : text) {
        if (c == 0) return false;
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') content = true;
    }
    return content;
}
bool validate_master(std::string_view text) noexcept {
    if (text.empty() || text.size() > max_text_size) return false;
    for (const unsigned char c : text) if (c == 0) return false;
    return true;
}

bool Checker::busy() const noexcept {
    return state >= State::opening_profile && state <= State::validating_master;
}
bool Checker::clear() noexcept {
    if (busy() || descriptor_ >= 0) return false;
    state = State::idle; failure = Failure::none; profile = {}; iso_path.fill(0); active_path.fill(0);
    operations = bytes_read = 0; open_result = read_result = close_result = error = 0;
    open_called = seek_called = read_called = close_called = false;
    after_close_ = State::done; pending_failure_ = Failure::none; limit_ = 0; path_.fill(0);
    return true;
}
bool Checker::request(std::string_view serial, std::string_view selected_iso_path) noexcept {
    if (busy() || descriptor_ >= 0) return false;
    clear();
    if (!serial_valid(serial) || selected_iso_path.empty() || selected_iso_path.size() >= iso_path.size() ||
        !selected_iso_path.starts_with("/data/PS2/isos/") || contains(selected_iso_path, "..")) {
        state = State::failed; failure = Failure::invalid_request; return false;
    }
    copy(selected_iso_path, iso_path.data(), iso_path.size());
    std::size_t at = 0;
    if (!append(path_.data(), path_.size(), at, catalog_directory) ||
        !append(path_.data(), path_.size(), at, serial) ||
        !append(path_.data(), path_.size(), at, ".profile")) {
        state = State::failed; failure = Failure::invalid_request; return false;
    }
    copy("PROFILE", active_path.data(), active_path.size());
    limit_ = max_profile_size; bytes_read = 0; state = State::opening_profile; return true;
}
void Checker::fail(Failure reason) noexcept {
    failure = reason;
    if (descriptor_ >= 0) finish_close(State::failed, reason);
    else state = State::failed;
}
void Checker::finish_close(State next, Failure close_failure) noexcept {
    after_close_ = next; pending_failure_ = close_failure; state = State::closing_profile;
}
bool Checker::begin_open(const char* path, State reading, Failure reason,
                         std::size_t limit) noexcept {
    if (operations == max_operations) { fail(Failure::operation_limit); return false; }
    ++operations; open_called = true; open_result = open_file(path);
    if (open_result < 0) { error = last_error(); descriptor_ = -1; fail(reason); return false; }
    descriptor_ = open_result; limit_ = limit; bytes_read = 0; state = reading; return true;
}
void Checker::take_read(State closing, Failure read_failure,
                        Failure size_failure) noexcept {
    if (operations == max_operations) { fail(Failure::operation_limit); return; }
    const auto remaining = limit_ + 1 - bytes_read;
    const auto amount = remaining < read_chunk ? remaining : read_chunk;
    ++operations; read_called = true;
    read_result = read_file(descriptor_, buffer_.data() + bytes_read, amount);
    if (read_result < 0) { error = last_error(); fail(read_failure); return; }
    if (static_cast<std::size_t>(read_result) > amount) { fail(read_failure); return; }
    if (read_result == 0) { finish_close(closing); return; }
    bytes_read += static_cast<std::size_t>(read_result);
    if (bytes_read > limit_) { fail(size_failure); return; }
}
void Checker::close_current() noexcept {
    // A close is mandatory even after the non-close operation cap is reached.
    ++operations; close_called = true; close_result = close_file(descriptor_);
    if (close_result != 0) { error = last_error(); failure = Failure::close; state = State::failed; return; }
    descriptor_ = -1;
    if (pending_failure_ != Failure::none) { failure = pending_failure_; pending_failure_ = Failure::none; state = State::failed; }
    else state = after_close_;
}

void Checker::step() noexcept {
    switch (state) {
    case State::opening_profile:
        begin_open(path_.data(), State::reading_profile, Failure::profile_open, max_profile_size); return;
    case State::reading_profile:
        take_read(State::parsing_profile, Failure::profile_read, Failure::profile_too_large); return;
    case State::closing_profile: close_current(); return;
    case State::parsing_profile: {
        buffer_[bytes_read] = 0;
        const auto parsed = parse_profile({buffer_.data(), bytes_read},
            {path_.data() + sizeof(catalog_directory) - 1, 10}, profile);
        if (parsed == Parse::invalid) { fail(Failure::profile_invalid); return; }
        if (parsed == Parse::mismatch) { fail(Failure::profile_mismatch); return; }
        if (parsed == Parse::review) { fail(Failure::review_required); return; }
        state = State::opening_iso; copy("ISO SIZE", active_path.data(), active_path.size()); return;
    }
    case State::opening_iso:
        begin_open(iso_path.data(), State::sizing_iso, Failure::iso_open, 0); return;
    case State::sizing_iso: {
        if (operations == max_operations) { fail(Failure::operation_limit); return; }
        ++operations; seek_called = true; const auto size = seek_file_end(descriptor_);
        if (size < 0) { error = last_error(); fail(Failure::iso_size); return; }
        pending_failure_ = (profile.status == Status::as_is && profile.expected_size == 0) ||
            static_cast<std::uint64_t>(size) == profile.expected_size ? Failure::none : Failure::iso_size_mismatch;
        after_close_ = pending_failure_ == Failure::none ? State::opening_config : State::failed;
        state = State::closing_iso; return;
    }
    case State::closing_iso: close_current(); if (state == State::opening_config) copy("CONFIG", active_path.data(), active_path.size()); return;
    case State::opening_config:
        begin_open(profile.config.data(), State::reading_config, Failure::config_open, max_text_size); return;
    case State::reading_config:
        take_read(State::validating_config, Failure::config_read, Failure::config_too_large); return;
    case State::closing_config: close_current(); return;
    case State::validating_config:
        buffer_[bytes_read] = 0;
        if (!validate_config({buffer_.data(), bytes_read}, profile, iso_path.data())) { fail(Failure::config_invalid); return; }
        if (profile.lua[0] == '-') { state = State::opening_master; copy("MASTER", active_path.data(), active_path.size()); }
        else { state = State::opening_lua; copy("LUA", active_path.data(), active_path.size()); }
        return;
    case State::opening_lua:
        begin_open(profile.lua.data(), State::reading_lua, Failure::lua_open, max_text_size); return;
    case State::reading_lua:
        take_read(State::validating_lua, Failure::lua_read, Failure::lua_too_large); return;
    case State::closing_lua: close_current(); return;
    case State::validating_lua:
        buffer_[bytes_read] = 0;
        if (!validate_lua({buffer_.data(), bytes_read})) { fail(Failure::lua_invalid); return; }
        state = State::opening_master; copy("MASTER", active_path.data(), active_path.size()); return;
    case State::opening_master:
        begin_open(master_path, State::reading_master, Failure::master_open, max_text_size); return;
    case State::reading_master:
        take_read(State::validating_master, Failure::master_read, Failure::master_too_large); return;
    case State::closing_master: close_current(); return;
    case State::validating_master:
        buffer_[bytes_read] = 0;
        if (!validate_master({buffer_.data(), bytes_read})) { fail(Failure::master_invalid); return; }
        state = State::done; copy("READY", active_path.data(), active_path.size()); return;
    default: return;
    }
}

std::string_view Checker::summary() const noexcept {
    if (state == State::idle) return "CROSS READS ID THEN CHECKS EXTERNAL PROFILE";
    if (busy()) return "CHECKING EXTERNAL PROFILE AND REQUIRED FILES";
    if (state == State::done) return "READY TO APPLY - DRY RUN PASSED";
    switch (failure) {
    case Failure::invalid_request: return "INVALID SERIAL OR SELECTED ISO PATH";
    case Failure::operation_limit: return "PROFILE CHECK OPERATION LIMIT REACHED";
    case Failure::profile_open: return "EXTERNAL SERIAL PROFILE IS MISSING OR UNREADABLE";
    case Failure::profile_read: return "EXTERNAL SERIAL PROFILE READ FAILED";
    case Failure::profile_too_large: return "EXTERNAL SERIAL PROFILE EXCEEDS 4 KIB";
    case Failure::profile_invalid: return "EXTERNAL SERIAL PROFILE IS INVALID";
    case Failure::profile_mismatch: return "EXTERNAL PROFILE SERIAL DOES NOT MATCH ISO";
    case Failure::review_required: return "DATABASE MATCH EXISTS BUT REQUIRES REVIEW";
    case Failure::iso_open: return "SELECTED ISO REOPEN FAILED";
    case Failure::iso_size: return "SELECTED ISO SIZE CHECK FAILED";
    case Failure::iso_size_mismatch: return "ISO SIZE DOES NOT MATCH TESTED PROFILE";
    case Failure::config_open: return "MATCHED CLI PROFILE IS MISSING OR UNREADABLE";
    case Failure::config_read: return "MATCHED CLI PROFILE READ FAILED";
    case Failure::config_too_large: return "MATCHED CLI PROFILE EXCEEDS 64 KIB";
    case Failure::config_invalid: return "MATCHED CLI DOES NOT FIT ISO SERIAL PATH OR LUA";
    case Failure::lua_open: return "MATCHED LUA IS MISSING OR UNREADABLE";
    case Failure::lua_read: return "MATCHED LUA READ FAILED";
    case Failure::lua_too_large: return "MATCHED LUA EXCEEDS 64 KIB";
    case Failure::lua_invalid: return "MATCHED LUA IS EMPTY OR CONTAINS NUL";
    case Failure::master_open: return "EXTERNAL MASTER IS MISSING OR UNREADABLE";
    case Failure::master_read: return "EXTERNAL MASTER READ FAILED";
    case Failure::master_too_large: return "EXTERNAL MASTER EXCEEDS 64 KIB";
    case Failure::master_invalid: return "EXTERNAL MASTER IS EMPTY OR CONTAINS NUL";
    case Failure::close: return "FILE CLOSE FAILED - CLOSE APP BEFORE RETRY";
    default: return "PROFILE CHECK FAILED";
    }
}
} // namespace profile_probe
