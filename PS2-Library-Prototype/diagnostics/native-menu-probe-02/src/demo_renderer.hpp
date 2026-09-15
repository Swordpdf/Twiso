/*
 * ps5-native-app-boilerplate - Small CPU-rendered demonstration API.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keeps PS5 VideoOut setup and the bitmap font out of the starter main file.
 */

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace ps5::demo
{
enum class Color : std::uint32_t
{
    background = UINT32_C(0xff190d0a),
    panel = UINT32_C(0xff301f17),
    white = UINT32_C(0xffffffff),
    cyan = UINT32_C(0xffffff00),
    magenta = UINT32_C(0xffff00ff),
    yellow = UINT32_C(0xff00ffff),
};

class Canvas;
using DrawScene = void (*)(Canvas &) noexcept;
using UpdateScene = bool (*)() noexcept;
[[noreturn]] void run(DrawScene draw, UpdateScene update, std::string_view ready_message) noexcept;

class Canvas final
{
  public:
    void clear(Color color) noexcept;
    void rectangle(unsigned x, unsigned y, unsigned width, unsigned height, Color color) noexcept;
    // Filled rounded rectangle (radius clamped to the half extents).
    void round_rect(unsigned x, unsigned y, unsigned width, unsigned height, unsigned radius,
                    Color color) noexcept;
    // Rounded rectangle drawn as a `thickness`-pixel border plus inner fill.
    void round_frame(unsigned x, unsigned y, unsigned width, unsigned height, unsigned radius,
                     unsigned thickness, Color border, Color fill) noexcept;
    void circle(unsigned center_x, unsigned center_y, unsigned radius, Color color) noexcept;
    void triangle(unsigned center_x, unsigned top, unsigned half_width, unsigned height,
                  Color color) noexcept;
    void text(unsigned x, unsigned y, std::string_view value, unsigned scale, Color color) noexcept;
    // Width in pixels the SST/UI atlas will use for value at the given scale.
    [[nodiscard]] unsigned text_width(std::string_view value, unsigned scale) const noexcept;
    // Starting x so the drawn ink (not the advance box) is centered on center.
    [[nodiscard]] unsigned text_center_start(unsigned center, std::string_view value,
                                             unsigned scale) const noexcept;
    // Draws a button glyph (ui_icons::Id) at its natural pixel height, tinted.
    void icon(unsigned x, unsigned y, unsigned glyph_id, Color color) noexcept;
    // Nearest-neighbour scaled blit of an RGBA8 image (row-major,
    // source_width*source_height*4 bytes). Fully transparent pixels are
    // skipped; everything else is alpha-blended over the surface. Used to
    // present decoded PS2 cover art inside a fixed pane.
    void blit_rgba(unsigned x, unsigned y, unsigned width, unsigned height,
                   std::span<const std::uint8_t> rgba, unsigned source_width,
                   unsigned source_height) noexcept;
    // Same, but clips the image to a rounded rectangle (radius in dest pixels).
    void blit_rgba_round(unsigned x, unsigned y, unsigned width, unsigned height, unsigned radius,
                         std::span<const std::uint8_t> rgba, unsigned source_width,
                         unsigned source_height) noexcept;

  private:
    explicit Canvas(std::uint32_t *pixels) noexcept : pixels_{pixels}
    {
    }

    std::uint32_t *pixels_;

    friend void run(DrawScene draw, UpdateScene update, std::string_view ready_message) noexcept;
};

void read_asset_text(const char *path, std::span<char> destination,
                     std::string_view fallback) noexcept;
} // namespace ps5::demo
