// Read-only, bounded selected-ISO identifier. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace serial_probe {
inline constexpr std::size_t sector_size = 2048;
inline constexpr std::size_t max_root_size = 1024 * 1024;
inline constexpr std::size_t max_cnf_size = 4096;
inline constexpr std::size_t max_operations = 1100;
inline constexpr std::size_t buffer_alignment = 0x4000;

int open_iso(const char* path) noexcept;
std::int64_t seek_iso(int descriptor, std::uint64_t offset) noexcept;
int read_iso(int descriptor, void* buffer, std::size_t size) noexcept;
int close_iso(int descriptor) noexcept;
int last_error() noexcept;

enum class State {
    idle, opening, seeking_pvd, reading_pvd, seeking_root, reading_root,
    seeking_cnf, reading_cnf, closing, done, failed
};
enum class Failure {
    none, invalid_name, path_too_long, open, seek, read, invalid_iso,
    system_cnf_missing, invalid_system_cnf, serial_missing, close, operation_limit
};

class Scanner final {
public:
    State state = State::idle;
    Failure failure = Failure::none;
    std::array<char, 272> path{};
    std::array<char, 11> serial{};
    std::size_t operations = 0;
    int open_result = 0, read_result = 0, close_result = 0, error = 0;
    std::uint64_t last_offset = 0;
    bool open_called = false, seek_called = false, read_called = false, close_called = false;

    bool busy() const noexcept;
    bool request(std::string_view filename) noexcept;
    bool clear() noexcept;
    void step() noexcept; // At most one platform I/O call per invocation.
    std::string_view summary() const noexcept;

private:
    int descriptor_ = -1;
    State after_close_ = State::done;
    unsigned pvd_sector_ = 16;
    std::uint64_t root_offset_ = 0, cnf_offset_ = 0;
    std::uint32_t root_size_ = 0, root_done_ = 0, cnf_size_ = 0;
    std::size_t wanted_ = 0, filled_ = 0;
    alignas(buffer_alignment) std::array<unsigned char, max_cnf_size + 1> buffer_{};

    void fail(Failure reason) noexcept;
    void finish(State result) noexcept;
    bool begin_seek(State state, std::uint64_t offset, std::size_t wanted) noexcept;
    bool take_read() noexcept;
    void parse_pvd() noexcept;
    void parse_root() noexcept;
    void parse_cnf() noexcept;
};
static_assert(alignof(Scanner) == buffer_alignment);
} // namespace serial_probe
