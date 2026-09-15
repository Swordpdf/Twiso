// Read-only external profile and launch-readiness checker. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace profile_probe {
inline constexpr char catalog_directory[] = "/data/PS2/library/catalog/";
inline constexpr char master_path[] = "/data/PS2/configs/master.txt";
inline constexpr std::size_t max_profile_size = 4096;
inline constexpr std::size_t max_text_size = 65536;
inline constexpr std::size_t read_chunk = 4096;
inline constexpr std::size_t max_operations = 64;
inline constexpr std::size_t buffer_alignment = 0x4000;

int open_file(const char* path) noexcept;
std::int64_t seek_file_end(int descriptor) noexcept;
int read_file(int descriptor, void* buffer, std::size_t size) noexcept;
int close_file(int descriptor) noexcept;
int last_error() noexcept;

enum class Status { none, console_tested, review_required, as_is };
enum class Parse { ok, invalid, mismatch, review };
enum class State {
    idle,
    opening_profile, reading_profile, closing_profile, parsing_profile,
    opening_iso, sizing_iso, closing_iso,
    opening_config, reading_config, closing_config, validating_config,
    opening_lua, reading_lua, closing_lua, validating_lua,
    opening_master, reading_master, closing_master, validating_master,
    done, failed
};
enum class Failure {
    none, invalid_request, operation_limit,
    profile_open, profile_read, profile_too_large, profile_invalid,
    profile_mismatch, review_required,
    iso_open, iso_size, iso_size_mismatch,
    config_open, config_read, config_too_large, config_invalid,
    lua_open, lua_read, lua_too_large, lua_invalid,
    master_open, master_read, master_too_large, master_invalid,
    close
};

struct Profile {
    std::array<char, 11> serial{};
    std::array<char, 81> name{};
    std::array<char, 33> backend{};
    std::array<char, 17> launcher{};
    std::array<char, 257> config{};
    std::array<char, 257> lua{};
    std::uint64_t expected_size = 0;
    Status status = Status::none;
};

Parse parse_profile(std::string_view text, std::string_view expected_serial,
                    Profile& output) noexcept;
bool validate_config(std::string_view text, const Profile& profile,
                     std::string_view iso_path) noexcept;
// For an explicit as-is profile, replace the CLI's static image option with
// the ISO selected by the native menu before the master is applied.
bool rewrite_as_is_image(char* text, std::size_t capacity, std::size_t& size,
                         std::string_view iso_path) noexcept;
// Helper used by the opt-in shared-VMC launcher build. The default launcher
// leaves the maker's /tmp/vmc setting unchanged; callers must explicitly opt
// into this rewrite after validating that the external directory is mounted.
bool rewrite_vmc_path(char* text, std::size_t capacity, std::size_t& size,
                      std::string_view serial) noexcept;
bool validate_lua(std::string_view text) noexcept;
bool validate_master(std::string_view text) noexcept;

class Checker final {
public:
    State state = State::idle;
    Failure failure = Failure::none;
    Profile profile{};
    std::array<char, 272> iso_path{};
    std::array<char, 64> active_path{};
    std::size_t operations = 0;
    std::size_t bytes_read = 0;
    int open_result = 0, read_result = 0, close_result = 0, error = 0;
    bool open_called = false, seek_called = false, read_called = false, close_called = false;

    bool busy() const noexcept;
    bool request(std::string_view serial, std::string_view selected_iso_path) noexcept;
    bool clear() noexcept;
    void step() noexcept; // At most one platform I/O call per invocation.
    std::string_view summary() const noexcept;

private:
    int descriptor_ = -1;
    State after_close_ = State::done;
    Failure pending_failure_ = Failure::none;
    std::size_t limit_ = 0;
    alignas(buffer_alignment) std::array<char, max_text_size + 1> buffer_{};
    std::array<char, 289> path_{};

    void fail(Failure reason) noexcept;
    void finish_close(State next, Failure close_failure = Failure::none) noexcept;
    bool begin_open(const char* path, State reading, Failure reason,
                    std::size_t limit) noexcept;
    void take_read(State closing, Failure read_failure,
                   Failure size_failure) noexcept;
    void close_current() noexcept;
};
static_assert(alignof(Checker) == buffer_alignment);
} // namespace profile_probe
