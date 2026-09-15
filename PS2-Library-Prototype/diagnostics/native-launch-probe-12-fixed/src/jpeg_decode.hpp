// Minimal baseline + progressive JPEG decoder to RGBA8.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Self-contained: no libc allocation, no new dynamic imports, static working
// set. Handles the subset produced by ps2-covers plus variants: sequential
// DCT (SOF0) and progressive DCT (SOF2), 8-bit, 1 or 3 components, YCbCr
// 4:2:0 / 4:2:2 / 4:4:4, restart intervals, and any progressive scan script.
// Arithmetic coding, 12-bit, and CMYK are rejected so the UI can show a
// placeholder instead of garbage.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace jpeg_probe {

// Covers are 512x736; allow a margin but keep the working set static.
inline constexpr int max_width = 1024;
inline constexpr int max_height = 1024;
// Component planes are decoded at up to 2x2 subsampling.
inline constexpr std::size_t plane_capacity =
    static_cast<std::size_t>(max_width) * max_height;

enum class Result { ok, too_small, bad_magic, unsupported, corrupt, out_of_bounds };

// Decodes a JPEG into \p rgba (width*height*4, RGBA8, row-major). The caller
// owns the buffer and must size it for the returned dimensions.
struct Image {
    int width = 0;
    int height = 0;
};

Result decode(std::span<const std::uint8_t> jpeg, std::span<std::uint8_t> rgba,
              Image &out) noexcept;

const char *summary(Result result) noexcept;

} // namespace jpeg_probe
