// On-demand PS2 cover art: load <SERIAL>.jpg from /data/PS2/covers and decode
// it to RGBA8 for the menu preview pane. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "jpeg_decode.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cover_probe {
inline constexpr char cover_dir[] = "/data/PS2/covers/";
inline constexpr std::size_t max_serial = 24;
inline constexpr std::size_t max_path = 96;
inline constexpr std::size_t max_jpeg_bytes = 1U << 20; // 1 MiB (covers are ~130 KiB)
inline constexpr std::size_t read_chunk = 64U << 10;

enum class State { idle, opening, reading, decoding, done, missing, failed };
enum class Failure {
    none,
    invalid_serial,
    open,
    read,
    too_large,
    empty,
    decode,
};

// Reads one JPEG cover at a time. `step()` performs at most one platform I/O
// (or one decode) so the render loop stays responsive; mirroring the other
// stores, it is driven to completion by repeatedly calling `step()`.
class Store final {
  public:
    State state = State::idle;
    Failure failure = Failure::none;
    std::size_t operations = 0;
    int width = 0;
    int height = 0;

    bool busy() const noexcept;
    bool clear() noexcept;
    bool request(std::string_view serial) noexcept;
    // Loads an explicit JPEG path. Used by host previews and tests; the
    // console flow always goes through request(serial).
    bool request_file(std::string_view path) noexcept;
    void step() noexcept;
    std::string_view summary() const noexcept;
    std::span<const std::uint8_t> pixels() const noexcept;

  private:
    std::array<char, max_path> jpeg_path_{};
    std::array<std::uint8_t, max_jpeg_bytes> jpeg_{};
    std::array<std::uint8_t, jpeg_probe::max_width * jpeg_probe::max_height * 4> pixels_{};
    std::size_t jpeg_size_ = 0;
    int descriptor_ = -1;

    void close_current() noexcept;
    void begin_failure(Failure reason) noexcept;
};
} // namespace cover_probe
