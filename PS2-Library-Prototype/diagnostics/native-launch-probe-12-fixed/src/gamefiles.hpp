// Per-game CLI/Lua file management for PS2 Library. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace apply_probe {
// Platform boundary (defined by apply_io.cpp on console, faked in tests).
int open_read_only(const char* path) noexcept;
int create_exclusive(const char* path) noexcept;
int read_data(int descriptor, void* buffer, std::size_t size) noexcept;
int write_data(int descriptor, const void* buffer, std::size_t size) noexcept;
int sync_data(int descriptor) noexcept;
int close_data(int descriptor) noexcept;
int remove_file(const char* path) noexcept;
int last_error() noexcept;
} // namespace apply_probe

namespace gamefiles {
inline constexpr char configs_prefix[] = "/data/PS2/configs/";
inline constexpr std::size_t max_base_length = 64;
inline constexpr std::size_t max_file_bytes = 65536;
inline constexpr std::size_t read_chunk = 4096;
inline constexpr std::size_t max_operations = 4000;
inline constexpr std::size_t buffer_alignment = 0x4000;

// Platform boundary reused from apply_probe (apply_store.hpp), so no new
// imports: open_read_only, create_exclusive, read_data, write_data,
// sync_data, close_data, remove_file, last_error.

enum class State {
    idle,
    checking_txt, reading_txt, validating_txt,
    checking_lua, reading_lua,
    closing, validating, txt_created,
    creating_txt, writing_txt, syncing_txt,
    creating_lua, writing_lua, syncing_lua,
    done, missing, failed
};
enum class Failure {
    none, invalid_request, invalid_id, operation_limit,
    txt_open, lua_open, txt_read, lua_read, txt_too_large, lua_too_large,
    txt_invalid, lua_invalid,
    txt_create, txt_write, txt_sync, lua_create, lua_write, lua_sync, close
};

class Files final {
public:
    State state = State::idle;
    Failure failure = Failure::none;
    bool need_txt = false, need_lua = false, created_any = false;
    std::size_t operations = 0;
    int open_result = 0, read_result = 0, write_result = 0, sync_result = 0, close_result = 0, error = 0;
    std::array<char, 65> active_path{};
    std::array<char, 160> txt_path{};
    std::array<char, 160> lua_path{};

    bool busy() const noexcept;
    bool clear() noexcept;
    // Begin ensure flow for a scanned ISO. Config stem is the game serial
    // (e.g. SLES-52017.txt/.lua), so renames never orphan game files.
    bool request(std::string_view iso_path, std::string_view serial) noexcept;
    // After missing: create defaults for whichever files are absent.
    bool create_missing() noexcept;
    void step() noexcept; // At most one platform I/O operation per call.
    std::string_view summary() const noexcept;

    // Pure helpers, fully host-testable without I/O.
    static bool valid_serial(std::string_view serial) noexcept;
    static bool build_loader(std::string_view stem, char* out, std::size_t capacity, std::size_t& size) noexcept;
    static bool build_default_txt(std::string_view stem, std::string_view iso_path,
                                  std::string_view serial, char* out,
                                  std::size_t capacity, std::size_t& size) noexcept;
    static bool build_default_lua(char* out, std::size_t capacity, std::size_t& size) noexcept;
    static bool validate_game_txt(std::string_view text, std::string_view iso_path,
                                  std::string_view serial) noexcept;

private:
    int descriptor_ = -1;
    State after_close_ = State::idle;
    std::size_t read_size_ = 0, written_ = 0, write_size_ = 0;
    bool txt_ok_ = false;
    std::array<char, 272> iso_path_{};
    std::array<char, 11> serial_{};
    alignas(buffer_alignment) std::array<char, max_file_bytes + 1> buffer_{};

    bool operation() noexcept;
    void begin_failure(Failure reason) noexcept;
    void take_read(State next, Failure read_failure, Failure size_failure) noexcept;
    void close_current() noexcept;
    void begin_create(const char* path, const char* label, State writing, Failure reason) noexcept;
    void take_write(State next, Failure reason) noexcept;
    void take_sync(State next, Failure reason) noexcept;
};
} // namespace gamefiles
