// Host-only checks for the bundled baseline+progressive JPEG decoder.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage: jpeg-tests <cover-dir> [dump-dir]
// Decodes every *.jpg in the cover directory, asserts geometry/content, and
// (when dump-dir is given) writes <name>.rgba (width*height*4, RGBA8) so the
// output can be compared against an independent decoder.
#include "jpeg_decode.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

std::vector<std::uint8_t> read_file(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void run_one(const std::filesystem::path &path, const std::filesystem::path &dump_dir)
{
    const auto bytes = read_file(path);
    std::printf("%-28s size=%zu\n", path.filename().string().c_str(), bytes.size());
    check(!bytes.empty(), "cover is readable");

    std::vector<std::uint8_t> rgba(jpeg_probe::max_width * jpeg_probe::max_height * 4);
    jpeg_probe::Image image;
    const auto result = jpeg_probe::decode(bytes, rgba, image);
    if (result != jpeg_probe::Result::ok)
    {
        std::printf("  decode failed: %s\n", jpeg_probe::summary(result));
        ++failures;
        return;
    }
    std::printf("  decoded %dx%d\n", image.width, image.height);
    check(image.width == 512 && image.height == 736, "cover geometry is 512x736");

    std::uint64_t sum = 0;
    std::uint8_t mn = 255, mx = 0;
    bool opaque = true;
    const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
    for (std::size_t i = 0; i < pixels; ++i)
    {
        const std::uint8_t r = rgba[i * 4 + 0];
        const std::uint8_t g = rgba[i * 4 + 1];
        const std::uint8_t b = rgba[i * 4 + 2];
        if (rgba[i * 4 + 3] != 255)
            opaque = false;
        const auto luma = static_cast<std::uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
        sum += luma;
        if (luma < mn)
            mn = luma;
        if (luma > mx)
            mx = luma;
    }
    const double average = static_cast<double>(sum) / static_cast<double>(pixels);
    std::printf("  luma avg=%.1f min=%u max=%u\n", average, mn, mx);
    check(opaque, "alpha channel is opaque");
    check(mx - mn > 16, "image is not a flat field");
    check(average > 8.0 && average < 248.0, "image luma is plausible");

    if (!dump_dir.empty())
    {
        std::filesystem::create_directories(dump_dir);
        std::ofstream out(dump_dir / (path.stem().string() + ".rgba"), std::ios::binary);
        out.write(reinterpret_cast<const char *>(rgba.data()),
                  static_cast<std::streamsize>(pixels * 4));
    }
}

} // namespace

int main(int argc, char **argv)
{
    const std::filesystem::path dir = (argc > 1) ? argv[1] : ".";
    const std::filesystem::path dump = (argc > 2) ? argv[2] : "";
    int count = 0;
    for (const auto &entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.path().extension() == ".jpg")
        {
            run_one(entry.path(), dump);
            ++count;
        }
    }
    if (count == 0)
    {
        std::printf("FAIL: no .jpg covers found in %s\n", dir.string().c_str());
        return 1;
    }

    {
        const std::uint8_t junk[] = {0x00, 0x01, 0x02, 0x03};
        std::vector<std::uint8_t> rgba(64);
        jpeg_probe::Image image;
        check(jpeg_probe::decode(junk, rgba, image) == jpeg_probe::Result::bad_magic,
              "garbage is rejected");
    }

    std::printf("\n%d covers checked, %d failures\n", count, failures);
    return failures == 0 ? 0 : 1;
}
