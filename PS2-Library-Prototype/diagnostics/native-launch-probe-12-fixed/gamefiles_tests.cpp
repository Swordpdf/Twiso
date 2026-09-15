// Host-only per-game file tests. No PS5 runtime or app launch.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/gamefiles.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>

namespace {
unsigned checks = 0;
std::array<int, 8> calls{}; // open-ro, create, read, write, sync, close, rename, unlink
std::map<std::string, std::string> files;
std::string active;
std::size_t position = 0;
int next_fd = 20, current_fd = -1, fake_error = 0;
int max_chunk = 1 << 20;
int fail_write = -1, fail_sync = -1, fail_close = -1;
void check(bool ok, int line) {
    ++checks; if (!ok) { std::fprintf(stderr, "Gamefiles check failed on line %d\n", line); std::abort(); }
}
#define CHECK(x) check((x), __LINE__)
const std::string iso_path = "/data/PS2/isos/lotr.iso";
const std::string serial = "SLES-52017";
std::string txt_path() { return "/data/PS2/configs/SLES-52017.txt"; }
std::string lua_path() { return "/data/PS2/configs/SLES-52017.lua"; }
std::string good_txt() {
    return "# SLES-52017\n--path-vmc=\"/data/PS2/saves/SLES-52017\"\n--ps2-title-id=SLES-52017\n"
        "--max-disc-num=1\n--image=\"/data/PS2/isos/lotr.iso\"\n--host-audio=1\n"
        "--rom=\"PS20220WD20050620.crack\"\n--host-display-mode=4:3\n";
}
std::string good_lua() { return "apiRequest(0.1)\nreturn true\n"; }
void reset() {
    calls = {}; files.clear(); active.clear(); position = 0; next_fd = 20; current_fd = -1; fake_error = 0;
    max_chunk = 1 << 20; fail_write = fail_sync = fail_close = -1;
}
int io_total() { int total = 0; for (int value : calls) total += value; return total; }
void run(gamefiles::Files& machine) {
    unsigned frames = 0;
    while (machine.busy() && frames++ < 20000) {
        const int before = io_total(); machine.step(); CHECK(io_total() - before <= 1);
    }
    CHECK(!machine.busy());
}
}

namespace apply_probe {
int open_read_only(const char* path) noexcept {
    ++calls[0]; const auto found = files.find(path);
    if (found == files.end()) { fake_error = ENOENT; return -1; }
    active = path; position = 0; current_fd = next_fd++; return current_fd;
}
int create_exclusive(const char* path) noexcept {
    ++calls[1]; if (files.contains(path)) { fake_error = EEXIST; return -1; }
    files[path] = {}; active = path; position = 0; current_fd = next_fd++; return current_fd;
}
int read_data(int fd, void* target, std::size_t size) noexcept {
    CHECK(fd == current_fd); ++calls[2];
    const auto& data = files[active]; if (position >= data.size()) return 0;
    const auto amount = std::min({size, static_cast<std::size_t>(max_chunk), data.size() - position});
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(position), amount, static_cast<char*>(target));
    position += amount; return static_cast<int>(amount);
}
int write_data(int fd, const void* source, std::size_t size) noexcept {
    CHECK(fd == current_fd); const int call = calls[3]++;
    if (call == fail_write) { fake_error = 5; return -1; }
    const auto amount = std::min(size, static_cast<std::size_t>(max_chunk));
    auto& data = files[active];
    if (position + amount > data.size()) data.resize(position + amount);
    std::copy_n(static_cast<const char*>(source), amount, data.begin() + static_cast<std::ptrdiff_t>(position));
    position += amount; return static_cast<int>(amount);
}
int sync_data(int fd) noexcept {
    CHECK(fd == current_fd); const int call = calls[4]++;
    if (call == fail_sync) { fake_error = 5; return -1; } return 0;
}
int close_data(int fd) noexcept {
    CHECK(fd == current_fd); const int call = calls[5]++; current_fd = -1; active.clear(); position = 0;
    if (call == fail_close) { fake_error = 9; return -1; } return 0;
}
int replace_file(const char*, const char*) noexcept { ++calls[6]; fake_error = 5; return -1; }
int remove_file(const char*) noexcept { ++calls[7]; fake_error = 5; return -1; }
int last_error() noexcept { return fake_error; }
} // namespace apply_probe

int main() {
    using namespace gamefiles;
    // Pure serial validation: any SLES/SLUS/SCUS/SCES form passes.
    {
        CHECK(Files::valid_serial("SLES-52017"));
        CHECK(Files::valid_serial("SLUS-97353"));
        CHECK(Files::valid_serial("SCUS-97353"));
        CHECK(Files::valid_serial("SCES-50000"));
        CHECK(!Files::valid_serial(""));
        CHECK(!Files::valid_serial("lower-12345"));
        CHECK(!Files::valid_serial("NOSUCHID!!"));
        CHECK(!Files::valid_serial("WAYTOOLONG-12345"));
    }
    // Pure loader + defaults builders keyed by serial.
    {
        std::array<char, 512> loader{}; std::size_t size = 0;
        CHECK(Files::build_loader("SLES-52017", loader.data(), loader.size(), size));
        const std::string_view view{loader.data(), size};
        CHECK(view.find("--config=\"/data/PS2/configs/SLES-52017.txt\"") != std::string_view::npos);
        CHECK(view.find("--config-local-lua=\"/data/PS2/configs/SLES-52017.lua\"") != std::string_view::npos);
        std::array<char, 16> tiny{}; std::size_t tiny_size = 0;
        CHECK(!Files::build_loader("SLES-52017", tiny.data(), tiny.size(), tiny_size));
        CHECK(!Files::build_loader("bad!!", loader.data(), loader.size(), size));
        std::array<char, 2048> txt{}; std::size_t txt_size = 0;
        CHECK(Files::build_default_txt("SLES-52017", iso_path, serial, txt.data(), txt.size(), txt_size));
        CHECK(Files::validate_game_txt({txt.data(), txt_size}, iso_path, serial));
        std::array<char, 64> lua{}; std::size_t lua_size = 0;
        CHECK(Files::build_default_lua(lua.data(), lua.size(), lua_size));
        CHECK(std::string_view(lua.data(), lua_size) == "apiRequest(0.1)\nreturn true\n");
    }
    // Pure txt validation rules.
    {
        CHECK(Files::validate_game_txt(good_txt(), iso_path, serial));
        CHECK(!Files::validate_game_txt("", iso_path, serial));
        std::string wrong_image = good_txt();
        wrong_image.replace(wrong_image.find("lotr.iso"), 8, "other.iso");
        CHECK(!Files::validate_game_txt(wrong_image, iso_path, serial));
        std::string wrong_title = good_txt();
        wrong_title.replace(wrong_title.find("--ps2-title-id=") + 15, 10, "SCUS-97353");
        CHECK(!Files::validate_game_txt(wrong_title, iso_path, serial));
        std::string tmp_vmc = good_txt();
        tmp_vmc.replace(tmp_vmc.find("/data/PS2/saves/SLES-52017"), 27, "/tmp/vmc");
        CHECK(!Files::validate_game_txt(tmp_vmc, iso_path, serial));
        CHECK(!Files::validate_game_txt(good_txt() + "--config=\"/data/x.txt\"\n", iso_path, serial));
        CHECK(!Files::validate_game_txt(good_txt() + "--config-local-lua=\"/data/y.lua\"\n", iso_path, serial));
        CHECK(!Files::validate_game_txt(good_txt() + "--image=\"/data/PS2/isos/lotr.iso\"\n", iso_path, serial));
        CHECK(!Files::validate_game_txt("# only a comment\n", iso_path, serial));
    }
    // Both present and valid: ready, zero writes.
    {
        reset(); files[txt_path()] = good_txt(); files[lua_path()] = good_lua();
        Files machine;
        CHECK(machine.request(iso_path, serial)); run(machine);
        CHECK(machine.state == State::done && !machine.need_txt && !machine.need_lua);
        CHECK(calls[1] == 0 && calls[3] == 0 && calls[4] == 0);
        CHECK(machine.summary() == "GAME FILES READY");
    }
    // Both absent: missing flags, then defaults created byte-exact.
    {
        reset(); Files machine;
        CHECK(machine.request(iso_path, serial)); run(machine);
        CHECK(machine.state == State::missing && machine.need_txt && machine.need_lua);
        CHECK(machine.create_missing());
        run(machine);
        CHECK(machine.state == State::done && machine.created_any);
        CHECK(machine.summary() == "GAME FILES READY - DEFAULTS CREATED");
        CHECK(Files::validate_game_txt(files[txt_path()], iso_path, serial));
        CHECK(files[lua_path()] == good_lua());
        CHECK(files[txt_path()].find("--ps2-title-id=SLES-52017\n") != std::string::npos);
        CHECK(files[txt_path()].find("--image=\"/data/PS2/isos/lotr.iso\"\n") != std::string::npos);
    }
    // Only lua missing: txt validated, lua created.
    {
        reset(); files[txt_path()] = good_txt(); Files machine;
        CHECK(machine.request(iso_path, serial)); run(machine);
        CHECK(machine.state == State::missing && !machine.need_txt && machine.need_lua);
        CHECK(machine.create_missing()); run(machine);
        CHECK(machine.state == State::done && files[lua_path()] == good_lua());
        CHECK(files[txt_path()] == good_txt());
    }
    // Invalid txt content fails with the rule message.
    {
        reset(); files[txt_path()] = "# empty\n"; files[lua_path()] = good_lua(); Files machine;
        CHECK(machine.request(iso_path, serial)); run(machine);
        CHECK(machine.state == State::failed && machine.failure == Failure::txt_invalid);
        CHECK(machine.summary() == "GAME TXT INVALID - FIX --image/--ps2-title-id/--path-vmc");
    }
    // Unusable serial fails before any I/O.
    {
        reset(); Files machine;
        CHECK(!machine.request(iso_path, "bad!!"));
        CHECK(machine.state == State::failed && machine.failure == Failure::invalid_id);
        CHECK(calls[0] == 0 && calls[1] == 0);
    }
    // Partial reads/writes still verify and create exactly.
    {
        reset(); max_chunk = 64; Files machine;
        CHECK(machine.request(iso_path, serial)); run(machine);
        CHECK(machine.state == State::missing);
        CHECK(machine.create_missing()); run(machine);
        CHECK(machine.state == State::done);
        CHECK(Files::validate_game_txt(files[txt_path()], iso_path, serial));
        CHECK(files[lua_path()] == good_lua());
    }
    // Write failure aborts creation without touching the other file.
    {
        reset(); fail_write = 0; Files machine;
        CHECK(machine.request(iso_path, serial)); run(machine);
        CHECK(machine.state == State::missing);
        CHECK(machine.create_missing()); run(machine);
        CHECK(machine.state == State::failed && machine.failure == Failure::txt_write);
        CHECK(!files.contains(lua_path()));
    }
    std::printf("Passed %u host checks: per-game file ensure, validation, and defaults.\n", checks);
}
