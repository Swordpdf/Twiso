// Read-only, bounded PS5 directory diagnostic. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace iso_probe {
inline constexpr char directory[] = "/data/PS2/isos";
inline constexpr std::size_t capacity = 128;
// Probe 05 hardware results: 4/16 KiB failed; 64/256 KiB succeeded.
// Reuse its tested size/alignment without increasing the aggregate read budget.
inline constexpr std::size_t buffer_size = 0x10000;
inline constexpr std::size_t buffer_alignment = 0x4000;
inline constexpr std::size_t max_read_bytes = 256 * 1024;
inline constexpr std::size_t max_batches = max_read_bytes / buffer_size;
static_assert(max_batches == 4 && max_batches * buffer_size == max_read_bytes);
inline constexpr std::size_t max_records = 4096;

// Adapter contract: native return values are preserved, not converted to errno.
int open_directory() noexcept;
int read_directory(int descriptor, char* buffer, int size) noexcept;
int close_directory(int descriptor) noexcept;

enum class State { idle, opening, reading, closing, done, limited, failed };
enum class Parse { ok, invalid, limit };

struct Name {
    std::array<char, 256> bytes{};
    std::size_t length = 0;
    std::string_view view() const noexcept { return {bytes.data(), length}; }
};

class Directory final {
public:
    State state = State::idle;
    std::array<Name, capacity> names{};
    std::size_t count = 0, records = 0, batches = 0, unknown_iso_types = 0;
    int open_result = 0, read_result = 0, close_result = 0;
    bool open_called = false, read_called = false, close_called = false;
    bool invalid_data = false;

    bool busy() const noexcept;
    bool request() noexcept; // Resets only in-memory results; performs no I/O.
    void step() noexcept;    // At most ONE native I/O call per update/frame.
    std::string_view summary() const noexcept;
    Parse consume(std::span<const unsigned char> data) noexcept;
private:
    int descriptor_ = -1;
    State after_close_ = State::done;
    alignas(buffer_alignment) std::array<char, buffer_size> buffer_{};
    void finish(State result) noexcept;
    void sort() noexcept;
};

static_assert(alignof(Directory) == buffer_alignment);

bool iso_name(std::string_view name) noexcept;
// Presentation only: raw names remain unchanged, unsupported glyphs become '?'.
void display_name(std::string_view name, std::span<char> output) noexcept;
} // namespace iso_probe
