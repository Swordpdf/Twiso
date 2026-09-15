// Host-only synthetic UI artifacts. Not evidence of PS5 I/O or launching.
// SPDX-License-Identifier: GPL-3.0-or-later
#define main unused_probe_main
#define PS2_LIBRARY_HOST_TEST 1
#include "src/main.cpp"
#undef main
#include "demo_renderer.cpp"
#include <bit>
#include <cstdio>
#include <type_traits>
#include <vector>

extern "C" int sceLncUtilLaunchApp(const char*, const char*[], LaunchContext*) { return -1; }

namespace {
bool bitmap(const char* name, const std::vector<std::uint32_t>& pixels) {
    std::array<unsigned char, 54> header{};
    const auto put32 = [&header](std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) header[at + i] = static_cast<unsigned char>(value >> (8 * i));
    };
    header[0] = 'B'; header[1] = 'M'; put32(2, 54 + 1920 * 1080 * 4); put32(10, 54); put32(14, 40);
    put32(18, 1920); put32(22, 1080); header[26] = 1; header[28] = 32; put32(34, 1920 * 1080 * 4);
    auto* out = std::fopen(name, "wb"); if (!out) return false;
    bool ok = std::fwrite(header.data(), 1, header.size(), out) == header.size();
    std::array<unsigned char, 1920 * 4> row{};
    for (int y = 1079; y >= 0 && ok; --y) {
        for (unsigned x = 0; x < 1920; ++x) {
            const auto pixel = pixels[ps5::demo::tiled_byte_offset(x, static_cast<unsigned>(y)) / sizeof(std::uint32_t)];
            row[x * 4] = static_cast<unsigned char>(pixel >> 16); row[x * 4 + 1] = static_cast<unsigned char>(pixel >> 8);
            row[x * 4 + 2] = static_cast<unsigned char>(pixel); row[x * 4 + 3] = 255;
        }
        ok = std::fwrite(row.data(), 1, row.size(), out) == row.size();
    }
    return std::fclose(out) == 0 && ok;
}
template <std::size_t N> void put(std::array<char, N>& output, std::string_view value) {
    output.fill(0); for (std::size_t i = 0; i < value.size() && i + 1 < N; ++i) output[i] = value[i];
}
}
int main() {
    static_assert(std::is_trivially_copyable_v<ps5::demo::Canvas>);
    std::vector<std::uint32_t> pixels(ps5::demo::frame_bytes / sizeof(std::uint32_t));
    auto canvas = std::bit_cast<ps5::demo::Canvas>(pixels.data());
    stage = Stage::input; status = "HOST LAYOUT PREVIEW - SYNTHETIC RESULTS"; last_button = "NOT A CONSOLE TEST";
    directory_scan.state = iso_probe::State::done; directory_scan.count = 2; directory_scan.batches = 2;
    constexpr std::string_view first_name = "Ratchet & Clank 3.iso";
    constexpr std::string_view second_name = "Lord of the Rings, The - The Return of the King (USA).iso";
    put(directory_scan.names[0].bytes, first_name); directory_scan.names[0].length = first_name.size();
    put(directory_scan.names[1].bytes, second_name); directory_scan.names[1].length = second_name.size();
    serial_scan.state = serial_probe::State::done; put(serial_scan.serial, "SCUS-97353");
    put(serial_scan.path, "/data/PS2/isos/Ratchet & Clank 3.iso");
    game_files.state = gamefiles::State::done;
    selected = 0; backend_choice = 1; focus = 2;
    screen = Screen::library; draw(canvas); if (!bitmap("build/preview-ready.bmp", pixels)) return 1;
    game_files.state = gamefiles::State::missing;
    game_files.need_txt = true; game_files.need_lua = true;
    focus = 0; backend_choice = 0;
    screen = Screen::missing_files; draw(canvas); if (!bitmap("build/preview-missing.bmp", pixels)) return 1;
    game_files.state = gamefiles::State::idle; game_files.need_txt = game_files.need_lua = false;
    backend_choice = 0; selected = 1;
    transaction.state = apply_probe::State::opening_master;
    screen = Screen::verifying_launch; draw(canvas); if (!bitmap("build/preview-verifying.bmp", pixels)) return 1;
    transaction.state = apply_probe::State::writing_stage; put(transaction.active_path, "STAGE WRITE");
    put(transaction.backup_path, "/data/PS2/configs/master.txt.library-backup-0001"); transaction.operations = 17;
    screen = Screen::applying; draw(canvas); if (!bitmap("build/preview-applying.bmp", pixels)) return 1;
    transaction.state = apply_probe::State::done; transaction.operations = 34; screen = Screen::applied;
    draw(canvas); if (!bitmap("build/preview-applied.bmp", pixels)) return 1;
    transaction.state = apply_probe::State::active_ready; put(transaction.active_path, "EXISTING MASTER MATCH");
    screen = Screen::confirm_privilege; draw(canvas); if (!bitmap("build/preview-confirm-privilege.bmp", pixels)) return 1;
    privilege.state = privilege_probe::State::ready; privilege.pid = 104; privilege.uid_before = 100; privilege.uid_after = 0;
    screen = Screen::confirm_launch; draw(canvas); if (!bitmap("build/preview-confirm-launch.bmp", pixels)) return 1;
    transaction.state = apply_probe::State::failed; transaction.failure = apply_probe::Failure::master_changed;
    put(transaction.active_path, "MASTER RECHECK"); screen = Screen::error;
    draw(canvas); if (!bitmap("build/preview-error.bmp", pixels)) return 1;
    screen = Screen::launch_result; launch_called = true; launch_code = static_cast<int>(0x80020002U);
    draw(canvas); if (!bitmap("build/preview-launch-failed.bmp", pixels)) return 1;
    screen = Screen::library; selected = 0; backend_choice = 1; focus = 0; list_first = 0;
    if (cover.request_file("/mnt/c/Users/spkro/AppData/Local/Temp/opencode/covers_probe/SCUS-97353.jpg")) {
        while (cover.busy()) cover.step();
    }
    draw(canvas); if (!bitmap("build/preview-cover.bmp", pixels)) return 1;
    focus = 1; draw(canvas); if (!bitmap("build/preview-cover-long.bmp", pixels)) return 1;
    focus = 2; draw(canvas); if (!bitmap("build/preview-library-buttons.bmp", pixels)) return 1;
    screen = Screen::settings; settings_focus = 1;
    draw(canvas); if (!bitmap("build/preview-settings.bmp", pixels)) return 1;
    screen = Screen::verifying_launch; transaction = {}; transaction.state = apply_probe::State::writing_stage;
    put(transaction.active_path, "STAGE WRITE"); transaction.operations = 12; cfg_opt[0] = true;
    draw(canvas); if (!bitmap("build/preview-loading.bmp", pixels)) return 1;
    std::puts("Rendered synthetic Probe 15 screens with the actual unchanged Canvas.");
}
