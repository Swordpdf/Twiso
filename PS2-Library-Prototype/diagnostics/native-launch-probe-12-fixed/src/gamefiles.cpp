// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamefiles.hpp"
#include <cerrno>

namespace gamefiles {
namespace {
bool same(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false;
    return true;
}
bool copy_text(const char* source, std::size_t size, char* target, std::size_t capacity) noexcept {
    if (size + 1 > capacity) return false;
    for (std::size_t i = 0; i < size; ++i) target[i] = source[i];
    target[size] = 0; return true;
}
bool copy_cstr(const char* source, char* target, std::size_t capacity) noexcept {
    std::size_t n = 0; while (source[n] != 0) ++n;
    return copy_text(source, n, target, capacity);
}
bool append_view(char* target, std::size_t capacity, std::size_t& at, std::string_view value) noexcept {
    if (at + value.size() + 1 > capacity) return false;
    for (char c : value) target[at++] = c;
    target[at] = 0; return true;
}
bool ascii_ok(std::string_view text) noexcept {
    for (const unsigned char c : text) {
        if (c == 0) return false;
        if (c < 32 && c != '\n' && c != '\r' && c != '\t') return false;
        if (c > 126) return false;
    }
    return true;
}
bool has_content(std::string_view text) noexcept {
    for (char c : text) if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return true;
    return false;
}
std::string_view unquote(std::string_view value) noexcept {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.remove_prefix(1); value.remove_suffix(1);
    }
    return value;
}
// Hand-rolled (no memchr/memmove): the container gate forbids new imports.
std::size_t find_newline(std::string_view text) noexcept {
    for (std::size_t i = 0; i < text.size(); ++i) if (text[i] == '\n') return i;
    return std::string_view::npos;
}
std::size_t find_equals(std::string_view line) noexcept {
    for (std::size_t i = 0; i < line.size(); ++i) if (line[i] == '=') return i;
    return std::string_view::npos;
}
bool starts_with(std::string_view text, std::string_view prefix) noexcept {
    if (text.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) if (text[i] != prefix[i]) return false;
    return true;
}
} // namespace

bool Files::busy() const noexcept {
    return state != State::idle && state != State::done &&
        state != State::missing && state != State::failed;
}
bool Files::clear() noexcept {
    if (busy() || descriptor_ >= 0) return false;
    state = State::idle; failure = Failure::none;
    need_txt = need_lua = created_any = txt_ok_ = false;
    operations = 0; open_result = read_result = write_result = sync_result = close_result = error = 0;
    active_path.fill(0); txt_path.fill(0); lua_path.fill(0);
    descriptor_ = -1; after_close_ = State::idle;
    read_size_ = 0, written_ = 0, write_size_ = 0;
    iso_path_.fill(0); serial_.fill(0);
    return true;
}
bool Files::operation() noexcept {
    if (operations >= max_operations) { begin_failure(Failure::operation_limit); return false; }
    ++operations; return true;
}
void Files::begin_failure(Failure reason) noexcept {
    if (failure == Failure::none) failure = reason;
    if (descriptor_ >= 0) {
        // Close first, then report failure (single-fd discipline).
        state = State::closing; after_close_ = State::failed;
    } else {
        state = State::failed;
    }
}
void Files::take_read(State next, Failure read_failure, Failure size_failure) noexcept {
    if (!operation()) return;
    const std::size_t room = buffer_.size() - 1 - read_size_;
    const std::size_t amount = room < read_chunk ? room : read_chunk;
    read_result = apply_probe::read_data(descriptor_, buffer_.data() + read_size_, amount);
    if (read_result < 0) { error = apply_probe::last_error(); begin_failure(read_failure); return; }
    if (static_cast<std::size_t>(read_result) > amount) { begin_failure(read_failure); return; }
    if (read_result == 0) { after_close_ = next; state = State::closing; return; }
    read_size_ += static_cast<std::size_t>(read_result);
    if (read_size_ > max_file_bytes) begin_failure(size_failure);
}
void Files::close_current() noexcept {
    ++operations; close_result = apply_probe::close_data(descriptor_); descriptor_ = -1;
    if (close_result != 0) {
        error = apply_probe::last_error();
        if (failure == Failure::none) failure = Failure::close;
        state = State::failed; return;
    }
    state = after_close_;
}
void Files::begin_create(const char* path, const char* label, State writing, Failure reason) noexcept {
    if (!operation()) return;
    copy_cstr(label, active_path.data(), active_path.size());
    open_result = apply_probe::create_exclusive(path);
    if (open_result < 0) {
        error = apply_probe::last_error();
        if (error == EEXIST) {
            // Placed between check and create: run one fresh verification pass.
            need_txt = need_lua = false; txt_ok_ = false;
            copy_cstr("RECHECK", active_path.data(), active_path.size());
            state = State::checking_txt; return;
        }
        begin_failure(reason); return;
    }
    descriptor_ = open_result; written_ = 0; state = writing;
}
void Files::take_write(State next, Failure reason) noexcept {
    if (written_ == write_size_) { state = next; return; }
    if (!operation()) return;
    const std::size_t left = write_size_ - written_;
    const std::size_t amount = left < read_chunk ? left : read_chunk;
    write_result = apply_probe::write_data(descriptor_, buffer_.data() + written_, amount);
    if (write_result <= 0 || static_cast<std::size_t>(write_result) > amount) {
        if (write_result < 0) error = apply_probe::last_error();
        begin_failure(reason); return;
    }
    written_ += static_cast<std::size_t>(write_result);
    if (written_ == write_size_) state = next;
}
void Files::take_sync(State next, Failure reason) noexcept {
    if (!operation()) return;
    sync_result = apply_probe::sync_data(descriptor_);
    if (sync_result != 0) { error = apply_probe::last_error(); begin_failure(reason); return; }
    after_close_ = next; state = State::closing;
}

bool Files::valid_serial(std::string_view serial) noexcept {
    if (serial.size() < 4 || serial.size() > 10) return false;
    bool dash = false;
    for (char c : serial) {
        if (c == '-') dash = true;
        else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return dash;
}
bool Files::build_loader(std::string_view stem, char* out, std::size_t capacity, std::size_t& size) noexcept {
    size = 0;
    if (out == nullptr || !valid_serial(stem)) return false;
    auto put = [&](std::string_view v) noexcept {
        if (size + v.size() + 1 > capacity) return false;
        for (char c : v) out[size++] = c;
        out[size] = 0; return true;
    };
    return put("# PS2 Library loader - ") && put(stem) &&
        put(". Managed on every launch; edit the game files instead.\n") &&
        put("--config=\"/data/PS2/configs/") && put(stem) && put(".txt\"\n") &&
        put("--config-local-lua=\"/data/PS2/configs/") && put(stem) && put(".lua\"\n");
}
bool Files::build_default_txt(std::string_view stem, std::string_view iso_path,
                              std::string_view serial, char* out,
                              std::size_t capacity, std::size_t& size) noexcept {
    size = 0;
    if (out == nullptr || !valid_serial(stem) || !valid_serial(serial) ||
        iso_path.empty() || iso_path.size() > 255) return false;
    auto put = [&](std::string_view v) noexcept {
        if (size + v.size() + 1 > capacity) return false;
        for (char c : v) out[size++] = c;
        out[size] = 0; return true;
    };
    return put("# ") && put(stem) &&
        put(" - PS2 Library per-game CLI. --image/--ps2-title-id/--path-vmc are load-bearing; other lines are yours.\n") &&
        put("--path-vmc=\"/data/PS2/saves/") && put(serial) && put("\"\n") &&
        put("--ps2-title-id=") && put(serial) && put("\n--max-disc-num=1\n--image=\"") &&
        put(iso_path) && put("\"\n--host-audio=1\n--rom=\"PS20220WD20050620.crack\"\n") &&
        put("--verbose-cdvd-reads=0\n--host-display-mode=4:3\n");
}
bool Files::build_default_lua(char* out, std::size_t capacity, std::size_t& size) noexcept {
    constexpr std::string_view text = "apiRequest(0.1)\nreturn true\n";
    size = 0;
    if (out == nullptr || text.size() + 1 > capacity) return false;
    for (char c : text) out[size++] = c;
    out[size] = 0;
    return true;
}
bool Files::validate_game_txt(std::string_view text, std::string_view iso_path,
                              std::string_view serial) noexcept {
    if (text.empty() || text.size() > max_file_bytes || !ascii_ok(text)) return false;
    char expected_vmc[64]{};
    {
        constexpr std::string_view prefix = "/data/PS2/saves/";
        if (prefix.size() + serial.size() + 1 > sizeof(expected_vmc)) return false;
        std::size_t at = 0;
        for (char c : prefix) expected_vmc[at++] = c;
        for (char c : serial) expected_vmc[at++] = c;
        expected_vmc[at] = 0;
    }
    const std::string_view want_vmc{expected_vmc};
    unsigned required = 0;
    while (!text.empty()) {
        const auto end = find_newline(text);
        auto line = std::string_view{text.data(), end == std::string_view::npos ? text.size() : end};
        text = end == std::string_view::npos ? std::string_view{} :
            std::string_view{text.data() + end + 1, text.size() - end - 1};
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto at = find_equals(line);
        if (!starts_with(line, "--") || at == std::string_view::npos || at < 3) return false;
        const auto key = std::string_view{line.data(), at};
        const auto value = unquote(std::string_view{line.data() + at + 1, line.size() - at - 1});
        unsigned bit = 0;
        if (same(key, "--ps2-title-id")) { bit = 1U << 0; if (!same(value, serial)) return false; }
        else if (same(key, "--image")) { bit = 1U << 1; if (!same(value, iso_path)) return false; }
        else if (same(key, "--path-vmc")) { bit = 1U << 2; if (!same(value, want_vmc)) return false; }
        else if (same(key, "--config") || starts_with(key, "--config-")) return false;
        if (bit != 0) {
            if ((required & bit) != 0) return false;
            required |= bit;
        }
    }
    return required == 0x7U;
}

bool Files::request(std::string_view iso_path, std::string_view serial) noexcept {
    if (!clear()) return false;
    if (iso_path.empty() || iso_path.size() > 255 || !valid_serial(serial)) {
        state = State::failed;
        failure = valid_serial(serial) ? Failure::invalid_request : Failure::invalid_id;
        return false;
    }
    if (!copy_text(iso_path.data(), iso_path.size(), iso_path_.data(), iso_path_.size()) ||
        !copy_text(serial.data(), serial.size(), serial_.data(), serial_.size())) {
        state = State::failed; failure = Failure::invalid_request; return false;
    }
    std::size_t at = 0;
    const std::string_view stem{serial_.data()};
    txt_path.fill(0); lua_path.fill(0);
    if (!append_view(txt_path.data(), txt_path.size(), at, configs_prefix) ||
        !append_view(txt_path.data(), txt_path.size(), at, stem) ||
        !append_view(txt_path.data(), txt_path.size(), at, ".txt")) {
        state = State::failed; failure = Failure::invalid_request; return false;
    }
    at = 0;
    if (!append_view(lua_path.data(), lua_path.size(), at, configs_prefix) ||
        !append_view(lua_path.data(), lua_path.size(), at, stem) ||
        !append_view(lua_path.data(), lua_path.size(), at, ".lua")) {
        state = State::failed; failure = Failure::invalid_request; return false;
    }
    copy_cstr("CHECK TXT", active_path.data(), active_path.size());
    state = State::checking_txt;
    return true;
}
bool Files::create_missing() noexcept {
    if (state != State::missing) return false;
    created_any = true;
    copy_cstr("CREATE DEFAULTS", active_path.data(), active_path.size());
    if (need_txt) {
        if (!build_default_txt({serial_.data()}, {iso_path_.data()}, {serial_.data()},
                               buffer_.data(), buffer_.size(), write_size_)) {
            state = State::failed; failure = Failure::txt_create; return false;
        }
        state = State::creating_txt;
        return true;
    }
    if (need_lua) {
        if (!build_default_lua(buffer_.data(), buffer_.size(), write_size_)) {
            state = State::failed; failure = Failure::lua_create; return false;
        }
        state = State::creating_lua;
        return true;
    }
    return false;
}

void Files::step() noexcept {
    switch (state) {
    case State::checking_txt: {
        if (!operation()) return;
        copy_cstr("CHECK TXT", active_path.data(), active_path.size());
        open_result = apply_probe::open_read_only(txt_path.data());
        if (open_result < 0) {
            error = apply_probe::last_error();
            if (error == ENOENT) {
                need_txt = true; copy_cstr("CHECK LUA", active_path.data(), active_path.size());
                state = State::checking_lua; return;
            }
            begin_failure(Failure::txt_open); return;
        }
        descriptor_ = open_result; read_size_ = 0; state = State::reading_txt; return;
    }
    case State::reading_txt:
        take_read(State::validating_txt, Failure::txt_read, Failure::txt_too_large); return;
    case State::closing: close_current(); return;
    case State::validating_txt: {
        buffer_[read_size_] = 0;
        txt_ok_ = validate_game_txt({buffer_.data(), read_size_}, {iso_path_.data()}, {serial_.data()});
        if (!txt_ok_) { begin_failure(Failure::txt_invalid); return; }
        copy_cstr("CHECK LUA", active_path.data(), active_path.size());
        state = State::checking_lua; return;
    }
    case State::checking_lua: {
        if (!operation()) return;
        copy_cstr("CHECK LUA", active_path.data(), active_path.size());
        open_result = apply_probe::open_read_only(lua_path.data());
        if (open_result < 0) {
            error = apply_probe::last_error();
            if (error == ENOENT) {
                need_lua = true; copy_cstr("DECIDE", active_path.data(), active_path.size());
                state = State::validating; return;
            }
            begin_failure(Failure::lua_open); return;
        }
        descriptor_ = open_result; read_size_ = 0; state = State::reading_lua; return;
    }
    case State::reading_lua:
        take_read(State::validating, Failure::lua_read, Failure::lua_too_large); return;
    case State::txt_created: {
        if (!need_lua) { copy_cstr("FILES READY", active_path.data(), active_path.size()); state = State::done; return; }
        if (!build_default_lua(buffer_.data(), buffer_.size(), write_size_)) {
            state = State::failed; failure = Failure::lua_create; return;
        }
        state = State::creating_lua; return;
    }
    case State::validating: {
        // Buffer holds the lua content when it was present.
        if (!need_txt && !txt_ok_) { begin_failure(Failure::txt_invalid); return; }
        if (!need_lua) {
            buffer_[read_size_] = 0;
            if (!has_content({buffer_.data(), read_size_})) { begin_failure(Failure::lua_invalid); return; }
        }
        if (need_txt || need_lua) { copy_cstr("FILES MISSING", active_path.data(), active_path.size()); state = State::missing; return; }
        copy_cstr("FILES READY", active_path.data(), active_path.size()); state = State::done; return;
    }
    case State::creating_txt:
        begin_create(txt_path.data(), "WRITE TXT", State::writing_txt, Failure::txt_create); return;
    case State::writing_txt:
        take_write(State::syncing_txt, Failure::txt_write); return;
    case State::syncing_txt:
        take_sync(State::txt_created, Failure::txt_sync); return;
    case State::creating_lua:
        begin_create(lua_path.data(), "WRITE LUA", State::writing_lua, Failure::lua_create); return;
    case State::writing_lua:
        take_write(State::syncing_lua, Failure::lua_write); return;
    case State::syncing_lua:
        take_sync(State::done, Failure::lua_sync); return;
    default: return;
    }
}

std::string_view Files::summary() const noexcept {
    if (state == State::idle) return "GAME FILES NOT CHECKED";
    if (state == State::done)
        return created_any ? "GAME FILES READY - DEFAULTS CREATED" : "GAME FILES READY";
    if (state == State::missing) return "GAME FILES MISSING - SEE PANEL";
    if (busy()) {
        if (state == State::creating_txt || state == State::writing_txt || state == State::syncing_txt ||
            state == State::creating_lua || state == State::writing_lua || state == State::syncing_lua)
            return "CREATING DEFAULT GAME FILES - DO NOT CLOSE";
        return "CHECKING GAME FILES - DO NOT CLOSE";
    }
    switch (failure) {
    case Failure::invalid_request: return "INVALID GAME REQUEST";
    case Failure::invalid_id: return "INVALID GAME ID - CANNOT MAP FILES";
    case Failure::operation_limit: return "FILE OPERATION LIMIT REACHED";
    case Failure::txt_open: return "GAME TXT OPEN FAILED";
    case Failure::lua_open: return "GAME LUA OPEN FAILED";
    case Failure::txt_read: return "GAME TXT READ FAILED";
    case Failure::lua_read: return "GAME LUA READ FAILED";
    case Failure::txt_too_large: case Failure::lua_too_large: return "GAME FILE TOO LARGE";
    case Failure::txt_invalid: return "GAME TXT INVALID - FIX --image/--ps2-title-id/--path-vmc";
    case Failure::lua_invalid: return "GAME LUA INVALID - MUST BE NON-EMPTY TEXT";
    case Failure::txt_create: case Failure::lua_create: return "DEFAULT CREATE FAILED";
    case Failure::txt_write: case Failure::lua_write: return "DEFAULT WRITE FAILED";
    case Failure::txt_sync: case Failure::lua_sync: return "DEFAULT SYNC FAILED";
    case Failure::close: return "FILE CLOSE FAILED - RESTART APP";
    default: return "GAME FILES FAILED";
    }
}
} // namespace gamefiles
