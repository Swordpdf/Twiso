/*
 * ps5-native-app-boilerplate - CPU VideoOut demonstration implementation.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Provides the bounded drawing surface and PS5 presentation loop used by the
 * editable starter application.
 */

#include "demo_renderer.hpp"
#include "ui_font.hpp"
#include "ui_icons.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>

extern "C"
{
    std::size_t sceKernelGetDirectMemorySize();
    int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end,
                                      std::size_t length, std::size_t alignment, int memory_type,
                                      std::int64_t *physical_address);
    int sceKernelMapDirectMemory(void **address, std::size_t length, int protection, int flags,
                                 std::int64_t physical_address, std::size_t alignment);
    int sceKernelSendNotificationRequest(std::uint32_t device, void *request, std::size_t size,
                                         int blocking);
    int sceKernelUsleep(std::uint32_t microseconds);
    int sceSystemServiceHideSplashScreen();
    int open(const char *path, int flags, ...);
    long read(int descriptor, void *buffer, std::size_t size);
    int close(int descriptor);
    int sceVideoOutOpen(std::int32_t user_id, std::int32_t bus_type, std::int32_t index,
                        const void *param);
    int sceVideoOutSetFlipRate(std::int32_t handle, std::int32_t rate);
    int sceVideoOutSubmitFlip(std::int32_t handle, std::int32_t buffer_index,
                              std::uint32_t flip_mode, std::int64_t flip_argument);
    int sceVideoOutWaitVblank(std::int32_t handle);
    bool ps5ObserveOwnedAllocation(const void *address) noexcept;
}

namespace ps5::demo
{
namespace
{
constexpr unsigned frame_width = 1920;
constexpr unsigned frame_height = 1080;
constexpr std::size_t frame_bytes = 0x1000000;
constexpr std::size_t memory_bytes = frame_bytes * 2;
constexpr std::size_t memory_alignment = 0x200000;
constexpr int memory_type_wc_garlic = 3;
constexpr int map_protection = 0x33;
constexpr std::uint64_t pixel_format_rgba8_srgb = UINT64_C(0x8000000022000000);

struct VideoBuffer
{
    void *data;
    void *metadata;
    void *reserved0;
    void *reserved1;
};

struct VideoAttribute
{
    std::uint8_t reserved[80];
};

extern "C" void sceVideoOutSetBufferAttribute2(VideoAttribute *attribute,
                                               std::uint64_t pixel_format,
                                               std::uint32_t tiling_mode, std::uint32_t width,
                                               std::uint32_t height, std::uint64_t option,
                                               std::uint32_t dcc_control,
                                               std::uint64_t dcc_clear_color);
extern "C" int sceVideoOutRegisterBuffers2(std::int32_t handle, std::int32_t set_index,
                                           std::int32_t buffer_index_start, VideoBuffer *buffers,
                                           std::int32_t buffer_count, VideoAttribute *attribute,
                                           std::int32_t category, void *option);

struct NotificationRequest
{
    std::uint8_t reserved[45];
    char message[3075];
};

class File final
{
  public:
    explicit File(int descriptor = -1) noexcept : descriptor_{descriptor}
    {
    }

    ~File()
    {
        reset();
    }

    File(const File &) = delete;
    File &operator=(const File &) = delete;

    File(File &&other) noexcept : descriptor_{std::exchange(other.descriptor_, -1)}
    {
    }

    File &operator=(File &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return descriptor_ >= 0;
    }

    [[nodiscard]] int get() const noexcept
    {
        return descriptor_;
    }

  private:
    void reset() noexcept
    {
        if (descriptor_ >= 0)
        {
            (void)close(descriptor_);
            descriptor_ = -1;
        }
    }

    int descriptor_;
};

class LifetimeProbe final
{
  public:
    explicit LifetimeProbe(bool &destroyed) noexcept : destroyed_{&destroyed}
    {
    }

    ~LifetimeProbe()
    {
        *destroyed_ = true;
    }

    LifetimeProbe(const LifetimeProbe &) = delete;
    LifetimeProbe &operator=(const LifetimeProbe &) = delete;

  private:
    bool *destroyed_;
};

NotificationRequest notification{};

void copy_message(std::span<char> destination, std::string_view source) noexcept
{
    if (destination.empty())
        return;

    const std::size_t count =
        source.size() < destination.size() - 1 ? source.size() : destination.size() - 1;
    for (std::size_t index = 0; index < count; ++index)
        destination[index] = source[index];
    destination[count] = '\0';
}

void notify(std::string_view message) noexcept
{
    copy_message(std::span{notification.message}, message);
    (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
}

[[nodiscard]] bool verify_unique_ownership() noexcept
{
    bool destroyed = false;
    {
        std::unique_ptr<LifetimeProbe> probe{new (std::nothrow_t{}) LifetimeProbe{destroyed}};
        if (!ps5ObserveOwnedAllocation(probe.get()))
            return false;
    }
    return destroyed;
}

[[noreturn]] void halt(std::string_view message) noexcept
{
    notify(message);
    for (;;)
        (void)sceKernelUsleep(1000000);
}

[[nodiscard]] constexpr std::size_t tiled_byte_offset(unsigned x, unsigned y) noexcept
{
    const std::uint32_t offset = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^
                                 ((y << 8) & 0x4000U) ^ ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^
                                 ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
    const std::uint32_t blocks_per_row = (frame_width + 127U) >> 7;
    const std::uint32_t block_index = (y >> 7) * blocks_per_row + (x >> 7);

    return (static_cast<std::size_t>(block_index) << 16) + offset;
}

void fill_rect(std::uint32_t *pixels, unsigned x, unsigned y, unsigned width, unsigned height,
               Color color) noexcept;

unsigned integer_sqrt(unsigned value) noexcept
{
    unsigned root = 0;
    while ((root + 1) * (root + 1) <= value)
        ++root;
    return root;
}

void put_span(std::uint32_t *pixels, unsigned x0, unsigned x1, unsigned y, Color color) noexcept
{
    if (x1 > x0)
        fill_rect(pixels, x0, y, x1 - x0, 1, color);
}

// Integer-only rounded rectangle: draw per-row spans whose ends follow the
// corner arcs (no floats, so no new libm import).
void fill_round_rect(std::uint32_t *pixels, unsigned x, unsigned y, unsigned width,
                     unsigned height, unsigned radius, Color color) noexcept
{
    if (width == 0 || height == 0)
        return;
    if (radius * 2 > width)
        radius = width / 2;
    if (radius * 2 > height)
        radius = height / 2;
    for (unsigned row = 0; row < height; ++row)
    {
        unsigned inset = 0;
        if (radius != 0)
        {
            if (row < radius)
            {
                const unsigned dy = radius - 1 - row;
                inset = radius - integer_sqrt(radius * radius - dy * dy);
            }
            else if (row >= height - radius)
            {
                const unsigned dy = row - (height - radius);
                inset = radius - integer_sqrt(radius * radius - dy * dy);
            }
        }
        put_span(pixels, x + inset, x + width - inset, y + row, color);
    }
}

void put_pixel_unchecked(std::uint32_t *pixels, unsigned x, unsigned y, Color color) noexcept
{
    auto *bytes = reinterpret_cast<std::uint8_t *>(pixels);
    *reinterpret_cast<std::uint32_t *>(bytes + tiled_byte_offset(x, y)) =
        static_cast<std::uint32_t>(color);
}

std::uint32_t get_pixel_unchecked(std::uint32_t *pixels, unsigned x, unsigned y) noexcept
{
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(pixels);
    return *reinterpret_cast<const std::uint32_t *>(bytes + tiled_byte_offset(x, y));
}

void fill_rect(std::uint32_t *pixels, unsigned x, unsigned y, unsigned width, unsigned height,
               Color color) noexcept
{
    if (x >= frame_width || y >= frame_height)
        return;

    const unsigned right = width > frame_width - x ? frame_width : x + width;
    const unsigned bottom = height > frame_height - y ? frame_height : y + height;
    for (unsigned row = y; row < bottom; ++row)
    {
        for (unsigned column = x; column < right; ++column)
            put_pixel_unchecked(pixels, column, row, color);
    }
}

void fill_circle(std::uint32_t *pixels, unsigned center_x, unsigned center_y, unsigned radius,
                 Color color) noexcept
{
    const int signed_radius = static_cast<int>(radius);
    for (int y = -signed_radius; y <= signed_radius; ++y)
    {
        for (int x = -signed_radius; x <= signed_radius; ++x)
        {
            if (x * x + y * y <= signed_radius * signed_radius)
            {
                const int pixel_x = static_cast<int>(center_x) + x;
                const int pixel_y = static_cast<int>(center_y) + y;
                if (pixel_x >= 0 && pixel_y >= 0)
                {
                    const auto bounded_x = static_cast<unsigned>(pixel_x);
                    const auto bounded_y = static_cast<unsigned>(pixel_y);
                    if (bounded_x < frame_width && bounded_y < frame_height)
                        put_pixel_unchecked(pixels, bounded_x, bounded_y, color);
                }
            }
        }
    }
}

void fill_triangle(std::uint32_t *pixels, unsigned center_x, unsigned top, unsigned half_width,
                   unsigned height, Color color) noexcept
{
    if (height == 0 || center_x >= frame_width)
        return;

    for (unsigned row = 0; row < height; ++row)
    {
        const unsigned half = row * half_width / height;
        const unsigned left = half > center_x ? 0 : center_x - half;
        const unsigned right = half >= frame_width - center_x ? frame_width : center_x + half + 1;
        fill_rect(pixels, left, top + row, right - left, 1, color);
    }
}

void blend_pixel(std::uint32_t *pixels, unsigned x, unsigned y, std::uint32_t color,
                 unsigned coverage) noexcept
{
    if (coverage == 0)
        return;
    if (coverage >= 255)
    {
        put_pixel_unchecked(pixels, x, y, static_cast<Color>(color));
        return;
    }
    const std::uint32_t destination = get_pixel_unchecked(pixels, x, y);
    const unsigned inverse = 255 - coverage;
    const unsigned sr = color & 0xffU, sg = (color >> 8) & 0xffU, sb = (color >> 16) & 0xffU;
    const unsigned r = (sr * coverage + (destination & 0xffU) * inverse + 127) / 255;
    const unsigned g = (sg * coverage + ((destination >> 8) & 0xffU) * inverse + 127) / 255;
    const unsigned b = (sb * coverage + ((destination >> 16) & 0xffU) * inverse + 127) / 255;
    put_pixel_unchecked(pixels, x, y,
                        static_cast<Color>(UINT32_C(0xff000000) | (b << 16) | (g << 8) | r));
}

// Text is rendered from the checked-in SST glyph atlas: each glyph is stored
// at the exact pixel height for its scale, so no runtime font scaling is
// needed. scale N draws with a 7*N pixel line height, matching the old bitmap
// font's vertical metrics.
void draw_text(std::uint32_t *pixels, unsigned x, unsigned y, std::string_view value,
               unsigned scale, Color color) noexcept
{
    if (scale == 0)
        scale = 1;
    const auto *face = ui_font::face_for(7 * scale);
    const std::uint32_t argb = static_cast<std::uint32_t>(color);
    for (char character : value)
    {
        const auto *glyph = ui_font::glyph(*face, character);
        if (glyph == nullptr)
        {
            x += face->pixel_height / 3 + scale;
            continue;
        }
        for (unsigned row = 0; row < glyph->h; ++row)
        {
            for (unsigned column = 0; column < glyph->w; ++column)
            {
                const unsigned coverage = face->pixels[glyph->offset + row * glyph->w + column];
                if (coverage == 0)
                    continue;
                const int px = static_cast<int>(x) + glyph->x0 + static_cast<int>(column);
                const int py = static_cast<int>(y) + glyph->y0 + static_cast<int>(row);
                if (px < 0 || py < 0)
                    continue;
                blend_pixel(pixels, static_cast<unsigned>(px), static_cast<unsigned>(py), argb, coverage);
            }
        }
        x += glyph->advance;
        if (x >= frame_width)
            return;
    }
}

// Horizontal inset of a rounded rectangle's row (0 in the straight sections).
unsigned round_inset(unsigned row, unsigned height, unsigned radius) noexcept
{
    if (radius == 0)
        return 0;
    if (radius * 2 > height)
        radius = height / 2;
    if (row < radius)
    {
        const unsigned dy = radius - 1 - row;
        return radius - integer_sqrt(radius * radius - dy * dy);
    }
    if (row >= height - radius)
    {
        const unsigned dy = row - (height - radius);
        return radius - integer_sqrt(radius * radius - dy * dy);
    }
    return 0;
}

void blit_rgba_impl(std::uint32_t *pixels, unsigned x, unsigned y, unsigned width,
                    unsigned height, unsigned radius, std::span<const std::uint8_t> rgba,
                    unsigned source_width, unsigned source_height) noexcept
{
    if (width == 0 || height == 0 || source_width == 0 || source_height == 0)
        return;
    if (x >= frame_width || y >= frame_height)
        return;
    const std::size_t required =
        static_cast<std::size_t>(source_width) * source_height * 4;
    if (rgba.size() < required)
        return;
    if (radius * 2 > width)
        radius = width / 2;

    const unsigned right = width > frame_width - x ? frame_width : x + width;
    const unsigned bottom = height > frame_height - y ? frame_height : y + height;
    for (unsigned dy = y; dy < bottom; ++dy)
    {
        const unsigned inset = round_inset(dy - y, height, radius);
        const unsigned row_left = x + inset;
        const unsigned row_right = x + width - inset;
        const unsigned limit = row_right < right ? row_right : right;
        const unsigned sy = static_cast<unsigned>(
            static_cast<std::uint64_t>(dy - y) * source_height / height);
        const std::uint8_t *row = rgba.data() +
            static_cast<std::size_t>(sy) * source_width * 4;
        for (unsigned dx = row_left; dx < limit; ++dx)
        {
            const unsigned sx = static_cast<unsigned>(
                static_cast<std::uint64_t>(dx - x) * source_width / width);
            const std::uint8_t *p = row + static_cast<std::size_t>(sx) * 4;
            const unsigned alpha = p[3];
            if (alpha == 0)
                continue;
            std::uint32_t value;
            if (alpha == 255)
            {
                value = UINT32_C(0xff000000) |
                        (static_cast<std::uint32_t>(p[2]) << 16) |
                        (static_cast<std::uint32_t>(p[1]) << 8) | p[0];
            }
            else
            {
                const std::uint32_t dst = get_pixel_unchecked(pixels, dx, dy);
                const unsigned inverse = 255 - alpha;
                const unsigned r = (static_cast<unsigned>(p[0]) * alpha +
                                    (dst & 0xffU) * inverse + 127) / 255;
                const unsigned g = (static_cast<unsigned>(p[1]) * alpha +
                                    ((dst >> 8) & 0xffU) * inverse + 127) / 255;
                const unsigned b = (static_cast<unsigned>(p[2]) * alpha +
                                    ((dst >> 16) & 0xffU) * inverse + 127) / 255;
                value = UINT32_C(0xff000000) | (b << 16) | (g << 8) | r;
            }
            put_pixel_unchecked(pixels, dx, dy, static_cast<Color>(value));
        }
    }
}

void flush_range(void *address, std::size_t length) noexcept
{
    auto *at = static_cast<std::uint8_t *>(address);
    const auto *end = at + length;

    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}
} // namespace

void Canvas::clear(Color color) noexcept
{
    fill_rect(pixels_, 0, 0, frame_width, frame_height, color);
}

void Canvas::rectangle(unsigned x, unsigned y, unsigned width, unsigned height,
                       Color color) noexcept
{
    fill_rect(pixels_, x, y, width, height, color);
}

void Canvas::round_rect(unsigned x, unsigned y, unsigned width, unsigned height, unsigned radius,
                        Color color) noexcept
{
    fill_round_rect(pixels_, x, y, width, height, radius, color);
}

void Canvas::round_frame(unsigned x, unsigned y, unsigned width, unsigned height, unsigned radius,
                         unsigned thickness, Color border, Color fill) noexcept
{
    fill_round_rect(pixels_, x, y, width, height, radius, border);
    if (thickness * 2 < width && thickness * 2 < height)
    {
        const unsigned inner_radius = radius > thickness ? radius - thickness : 0;
        fill_round_rect(pixels_, x + thickness, y + thickness, width - 2 * thickness,
                        height - 2 * thickness, inner_radius, fill);
    }
}

void Canvas::circle(unsigned center_x, unsigned center_y, unsigned radius, Color color) noexcept
{
    fill_circle(pixels_, center_x, center_y, radius, color);
}

void Canvas::triangle(unsigned center_x, unsigned top, unsigned half_width, unsigned height,
                      Color color) noexcept
{
    fill_triangle(pixels_, center_x, top, half_width, height, color);
}

void Canvas::text(unsigned x, unsigned y, std::string_view value, unsigned scale,
                  Color color) noexcept
{
    draw_text(pixels_, x, y, value, scale, color);
}

unsigned Canvas::text_width(std::string_view value, unsigned scale) const noexcept
{
    if (scale == 0)
        scale = 1;
    const auto *face = ui_font::face_for(7 * scale);
    unsigned width = 0;
    for (char character : value)
    {
        const auto *glyph = ui_font::glyph(*face, character);
        width += glyph != nullptr ? glyph->advance : face->pixel_height / 3 + scale;
    }
    return width;
}

unsigned Canvas::text_center_start(unsigned center, std::string_view value,
                                   unsigned scale) const noexcept
{
    if (scale == 0)
        scale = 1;
    const auto *face = ui_font::face_for(7 * scale);
    int pen = 0, left = 0, right = 0;
    bool ink_seen = false;
    for (char character : value)
    {
        const auto *glyph = ui_font::glyph(*face, character);
        if (glyph == nullptr)
        {
            pen += static_cast<int>(face->pixel_height / 3 + scale);
            continue;
        }
        if (glyph->w != 0 && glyph->h != 0)
        {
            const int glyph_left = pen + glyph->x0;
            const int glyph_right = glyph_left + glyph->w;
            if (!ink_seen) { left = glyph_left; right = glyph_right; ink_seen = true; }
            else { if (glyph_left < left) left = glyph_left; if (glyph_right > right) right = glyph_right; }
        }
        pen += glyph->advance;
    }
    if (!ink_seen)
        return center;
    const int ink_center = (left + right) / 2;
    const int start = static_cast<int>(center) - ink_center;
    return start < 0 ? 0 : static_cast<unsigned>(start);
}

void Canvas::icon(unsigned x, unsigned y, unsigned glyph_id, Color color) noexcept
{
    if (glyph_id >= static_cast<unsigned>(ui_icons::Id::count))
        return;
    const auto *bitmap = ui_icons::bitmaps() + glyph_id;
    const std::uint8_t *coverage = ui_icons::pixels();
    const std::uint32_t argb = static_cast<std::uint32_t>(color);
    for (unsigned row = 0; row < bitmap->h; ++row)
    {
        for (unsigned column = 0; column < bitmap->w; ++column)
        {
            const unsigned alpha = coverage[bitmap->offset + row * bitmap->w + column];
            if (alpha == 0)
                continue;
            blend_pixel(pixels_, x + column, y + row, argb, alpha);
        }
    }
}

void Canvas::blit_rgba(unsigned x, unsigned y, unsigned width, unsigned height,
                       std::span<const std::uint8_t> rgba, unsigned source_width,
                       unsigned source_height) noexcept
{
    blit_rgba_impl(pixels_, x, y, width, height, 0, rgba, source_width, source_height);
}

void Canvas::blit_rgba_round(unsigned x, unsigned y, unsigned width, unsigned height,
                             unsigned radius, std::span<const std::uint8_t> rgba,
                             unsigned source_width, unsigned source_height) noexcept
{
    blit_rgba_impl(pixels_, x, y, width, height, radius, rgba, source_width, source_height);
}

void read_asset_text(const char *path, std::span<char> destination,
                     std::string_view fallback) noexcept
{
    copy_message(destination, fallback);
    File descriptor{open(path, 0)};
    if (!descriptor.valid() || destination.empty())
        return;

    const long count = read(descriptor.get(), destination.data(), destination.size() - 1);
    if (count <= 0)
        return;

    std::size_t length = static_cast<std::size_t>(count);
    while (length > 0 && (destination[length - 1] == '\r' || destination[length - 1] == '\n'))
        --length;
    destination[length] = '\0';
}

[[noreturn]] void run(DrawScene draw, UpdateScene update, std::string_view ready_message) noexcept
{
    if (!verify_unique_ownership())
        halt("Hello World: unique ownership failed");
    if (draw == nullptr)
        halt("Hello World: scene callback missing");

    (void)sceSystemServiceHideSplashScreen();
    const int video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (video < 0)
        halt("Hello World: sceVideoOutOpen failed");

    const std::size_t pool_size = sceKernelGetDirectMemorySize();
    if (pool_size < memory_bytes)
        halt("Hello World: insufficient direct memory");

    std::int64_t physical_address = 0;
    int result =
        sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(pool_size), memory_bytes,
                                      memory_alignment, memory_type_wc_garlic, &physical_address);
    if (result < 0)
        halt("Hello World: direct-memory allocation failed");

    void *mapped = nullptr;
    result = sceKernelMapDirectMemory(&mapped, memory_bytes, map_protection, 0, physical_address,
                                      memory_alignment);
    if (result < 0)
        halt("Hello World: direct-memory mapping failed");

    Canvas first{static_cast<std::uint32_t *>(mapped)};
    auto *second_frame = static_cast<std::uint8_t *>(mapped) + frame_bytes;
    Canvas second{reinterpret_cast<std::uint32_t *>(second_frame)};
    draw(first);
    draw(second);
    flush_range(mapped, memory_bytes);

    std::array<VideoBuffer, 2> buffers{{
        {mapped, nullptr, nullptr, nullptr},
        {second_frame, nullptr, nullptr, nullptr},
    }};
    VideoAttribute attribute{};
    (void)sceVideoOutSetFlipRate(video, 0);
    sceVideoOutSetBufferAttribute2(&attribute, pixel_format_rgba8_srgb, 0, frame_width,
                                   frame_height, 0, 0, 0);

    result = sceVideoOutRegisterBuffers2(video, 0, 0, buffers.data(),
                                         static_cast<std::int32_t>(buffers.size()), &attribute, 0,
                                         nullptr);
    if (result < 0)
        halt("Hello World: buffer registration failed");
    if (sceVideoOutSubmitFlip(video, 0, 1, 1) < 0)
        halt("Hello World: initial flip failed");

    (void)sceVideoOutWaitVblank(video);
    notify(ready_message);

    // Returning from main or calling exit crashes this launch context.
    // Redraw only after an input/state change, always into the back buffer.
    int current = 0;
    std::int64_t flip = 1;
    for (;;) {
        if (update != nullptr && update()) {
            const int next = 1 - current;
            if (next == 0) draw(first); else draw(second);
            flush_range(next == 0 ? mapped : second_frame, frame_bytes);
            if (sceVideoOutSubmitFlip(video, next, 1, ++flip) < 0)
                halt("PS2 Library: display update failed");
            (void)sceVideoOutWaitVblank(video);
            (void)sceVideoOutWaitVblank(video);
            current = next;
        }
        (void)sceKernelUsleep(16000);
    }
}
} // namespace ps5::demo
