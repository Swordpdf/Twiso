// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace ps2lib {
constexpr std::size_t max_config_size = 16384;
struct Profile {
    const char* name;
    const char* config_name;
    const char* serial;
    const char* image;
    const char* lua;
    std::uint64_t image_size;
    const char* backend_name;
    const char* launcher_id;
};
inline constexpr std::array<Profile, 2> profiles{{
    {"RATCHET AND CLANK 3", "rac3.txt", "SCUS-97353",
     "/data/PS2/isos/rac3.iso", "/data/PS2/configs/rac3.lua", 4379377664ULL, "WOTM V1", "TEST12345"},
    {"ULAUNCHELF BASELINE", "ulaunchelf.txt", "TEST-12345", "", "", 0, "WOTM V1", "TEST12345"}
}};
struct Result {
    bool ok = false;
    int error = 0;
    std::array<char, 160> message{};
    std::array<char, 768> backup{};
};
// The optional prefix maps the console's absolute paths into a host-test sandbox.
// Production always passes an empty prefix; no network or shell execution here.
Result validate(const Profile& profile, const char* prefix = "") noexcept;
Result validate_active(const Profile& profile, const char* prefix = "") noexcept;
Result apply(const Profile& profile, const char* prefix = "") noexcept;
Result validate_config(std::string_view text, const Profile& profile) noexcept;
}
