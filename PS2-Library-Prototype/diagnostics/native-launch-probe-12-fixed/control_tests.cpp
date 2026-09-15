// Host-only controller and launch-gating test. No PS5 runtime.
// SPDX-License-Identifier: GPL-3.0-or-later
#define main probe_application_main
#define PS2_LIBRARY_HOST_TEST 1
#include "src/main.cpp"
#undef main
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace {
unsigned checks = 0;
std::array<ps5::pad::Data, ps5::pad::kMaxSamples> queued{};
int queued_count = 0;
std::uint64_t timestamp = 1;
std::map<std::string, std::string> files;
std::string active_file, launched_title;
std::size_t file_position = 0;
int active_fd = -1, next_fd = 40, fake_error = 0, launch_calls = 0, launch_return = 0;
int privilege_uid = 100, privilege_exists = 0, privilege_error = 0;
std::string privilege_body;
std::array<int, 8> file_calls{};
void check(bool ok, int line) {
    ++checks; if (!ok) { std::fprintf(stderr, "Control check failed on line %d\n", line); std::abort(); }
}
#define CHECK(x) check((x), __LINE__)
std::string game_txt() {
    return "# SCUS-97353\n--path-vmc=\"/data/PS2/saves/SCUS-97353\"\n--ps2-title-id=SCUS-97353\n"
        "--max-disc-num=1\n--image=\"/data/PS2/isos/Ratchet & Clank 3.iso\"\n"
        "--host-audio=1\n--rom=\"PS20220WD20050620.crack\"\n--host-display-mode=4:3\n";
}
std::string game_lua() { return "apiRequest(0.1)\nreturn true\n"; }
std::string loader_config() {
    return "# PS2 Library loader - SCUS-97353. Managed on every launch; edit the game files instead.\n"
        "--config=\"/data/PS2/configs/SCUS-97353.txt\"\n"
        "--config-local-lua=\"/data/PS2/configs/SCUS-97353.lua\"\n";
}
std::string active_config() {
    return loader_config();
}
bool sample(std::uint32_t buttons, bool connected = true) {
    queued = {}; queued_count = 1; queued[0] = ps5::pad::neutral_data(timestamp++);
    queued[0].buttons = buttons; queued[0].connected = connected ? 1 : 0; return update();
}
void release() { (void)sample(0); }
void reseed_serial() {
    serial_scan.state = serial_probe::State::done;
    constexpr std::string_view serial = "SCUS-97353";
    std::copy(serial.begin(), serial.end(), serial_scan.serial.begin());
    constexpr std::string_view iso = "/data/PS2/isos/Ratchet & Clank 3.iso";
    std::copy(iso.begin(), iso.end(), serial_scan.path.begin());
}
void finish_background(Screen wanted) {
    unsigned frames = 0;
    queued_count = 0;
    while (screen != wanted && screen != Screen::error && frames++ < 1000) (void)update();
    CHECK(screen == wanted);
}
// Settle machines in place so the next press registers (busy frames consume
// the neutral re-arm). Queued input stays empty throughout.
void drain_machines() {
    unsigned frames = 0;
    queued_count = 0;
    while ((game_files.busy() || transaction.busy()) && frames++ < 20000) (void)update();
}
}

namespace iso_probe {
int open_directory() noexcept { return -1; }
int read_directory(int, char*, int) noexcept { return -1; }
int close_directory(int) noexcept { return 0; }
}
namespace serial_probe {
int open_iso(const char*) noexcept { return -1; }
std::int64_t seek_iso(int, std::uint64_t) noexcept { return -1; }
int read_iso(int, void*, std::size_t) noexcept { return -1; }
int close_iso(int) noexcept { return 0; }
int last_error() noexcept { return EIO; }
}
namespace profile_probe {
int open_file(const char*) noexcept { return -1; }
std::int64_t seek_file_end(int) noexcept { return -1; }
int read_file(int, void*, std::size_t) noexcept { return -1; }
int close_file(int) noexcept { return 0; }
int last_error() noexcept { return EIO; }
}
namespace apply_probe {
int open_read_only(const char* path) noexcept {
    ++file_calls[0]; const auto found = files.find(path); if (found == files.end()) { fake_error = ENOENT; return -1; }
    active_file = path; file_position = 0; active_fd = next_fd++; return active_fd;
}
int create_exclusive(const char* path) noexcept {
    ++file_calls[1]; if (files.contains(path)) { fake_error = EEXIST; return -1; }
    files[path] = {}; active_file = path; file_position = 0; active_fd = next_fd++; return active_fd;
}
int read_data(int fd, void* target, std::size_t size) noexcept {
    CHECK(fd == active_fd); ++file_calls[2]; const auto& data = files[active_file];
    if (file_position == data.size()) return 0;
    const auto amount = std::min(size, data.size() - file_position);
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(file_position), amount, static_cast<char*>(target));
    file_position += amount; return static_cast<int>(amount);
}
int write_data(int fd, const void* source, std::size_t size) noexcept {
    CHECK(fd == active_fd); ++file_calls[3]; auto& data = files[active_file];
    if (file_position + size > data.size()) data.resize(file_position + size);
    std::copy_n(static_cast<const char*>(source), size, data.begin() + static_cast<std::ptrdiff_t>(file_position));
    file_position += size; return static_cast<int>(size);
}
int sync_data(int fd) noexcept { CHECK(fd == active_fd); ++file_calls[4]; return 0; }
int close_data(int fd) noexcept {
    CHECK(fd == active_fd); ++file_calls[5]; active_fd = -1; active_file.clear(); file_position = 0; return 0;
}
int replace_file(const char* source, const char* target) noexcept {
    ++file_calls[6]; const auto found = files.find(source); CHECK(found != files.end());
    files[target] = found->second; files.erase(found); return 0;
}
int remove_file(const char* path) noexcept { ++file_calls[7]; return files.erase(path) == 1 ? 0 : -1; }
int last_error() noexcept { return fake_error; }
}
namespace privilege_probe {
int process_id() noexcept { return 104; }
int effective_user_id() noexcept { return privilege_uid; }
int remove_request(const char*) noexcept { return 0; }
int open_request(const char* path) noexcept { CHECK(std::string_view(path) == staged_request_path); privilege_body.clear(); return 70; }
int make_request_readable(int fd) noexcept { CHECK(fd == 70); return 0; }
int write_request(int fd, const void* data, std::size_t size) noexcept {
    CHECK(fd == 70); privilege_body.append(static_cast<const char*>(data), size); return static_cast<int>(size);
}
int sync_request(int fd) noexcept { CHECK(fd == 70); return 0; }
int close_request(int fd) noexcept { CHECK(fd == 70); return 0; }
int publish_request(const char* source, const char* target) noexcept {
    CHECK(std::string_view(source) == staged_request_path && std::string_view(target) == request_path); return 0;
}
int request_exists(const char*) noexcept { return privilege_exists; }
int last_error() noexcept { return privilege_error; }
}

extern "C" std::int32_t sceUserServiceInitialize(void*) { return 0; }
extern "C" std::int32_t sceUserServiceGetInitialUser(std::int32_t* value) { *value = 42; return 0; }
extern "C" std::int32_t scePadInit() { return 0; }
extern "C" std::int32_t scePadOpen(std::int32_t, std::int32_t, std::int32_t, const void*) { return 7; }
extern "C" std::int32_t scePadRead(std::int32_t handle, ps5::pad::Data* output, std::int32_t capacity) {
    CHECK(handle == 7 && capacity == static_cast<int>(ps5::pad::kMaxSamples));
    if (queued_count == 1) output[0] = queued[0];
    return queued_count;
}
extern "C" int sceSystemServiceLaunchApp(const char* title, char* arguments[], LaunchContext* context) {
    ++launch_calls; launched_title = title; CHECK(arguments && arguments[0] == nullptr);
    CHECK(context && context->size == sizeof(LaunchContext) && context->user == 42);
    return launch_return;
}
extern "C" int sceLncUtilLaunchApp(const char*, const char*[], LaunchContext*) { return -1; }

namespace ps5::demo {
void Canvas::clear(Color) noexcept {}
void Canvas::rectangle(unsigned x, unsigned y, unsigned width, unsigned height, Color) noexcept {
    CHECK(x + width <= 1920 && y + height <= 1080);
}
void Canvas::circle(unsigned, unsigned, unsigned, Color) noexcept {}
void Canvas::triangle(unsigned, unsigned, unsigned, unsigned, Color) noexcept {}
void Canvas::text(unsigned x, unsigned y, std::string_view value, unsigned scale, Color) noexcept {
    CHECK(x + value.size() * 6 * scale <= 1920 && y + 7 * scale <= 1080);
}
void Canvas::blit_rgba(unsigned x, unsigned y, unsigned width, unsigned height,
                       std::span<const std::uint8_t>, unsigned, unsigned) noexcept {
    CHECK(x + width <= 1920 && y + height <= 1080);
}
void Canvas::blit_rgba_round(unsigned x, unsigned y, unsigned width, unsigned height, unsigned,
                             std::span<const std::uint8_t>, unsigned, unsigned) noexcept {
    CHECK(x + width <= 1920 && y + height <= 1080);
}
unsigned Canvas::text_width(std::string_view value, unsigned scale) const noexcept {
    return static_cast<unsigned>(value.size()) * 6 * (scale == 0 ? 1 : scale);
}
unsigned Canvas::text_center_start(unsigned center, std::string_view value, unsigned scale) const noexcept {
    const unsigned half = text_width(value, scale) / 2;
    return center > half ? center - half : 0;
}
void Canvas::icon(unsigned x, unsigned y, unsigned, Color) noexcept {
    CHECK(x <= 1920 && y <= 1080);
}
void Canvas::round_rect(unsigned x, unsigned y, unsigned width, unsigned height, unsigned, Color) noexcept {
    CHECK(x + width <= 1920 && y + height <= 1080);
}
void Canvas::round_frame(unsigned x, unsigned y, unsigned width, unsigned height, unsigned, unsigned, Color, Color) noexcept {
    CHECK(x + width <= 1920 && y + height <= 1080);
}
void read_asset_text(const char*, std::span<char>, std::string_view) noexcept {}
[[noreturn]] void run(DrawScene drawing, UpdateScene, std::string_view) noexcept {
    Canvas canvas{nullptr}; stage = Stage::input;
    for (int value = 0; value <= static_cast<int>(Screen::error); ++value) { screen = static_cast<Screen>(value); drawing(canvas); }
    std::printf("Passed %u host checks: apply/read-only paths, HEN gating, launch gating, and UI bounds.\n", checks);
    std::exit(0);
}
}

// Hermetic cover stubs: the control test exercises menu navigation and the
// launch gate, not cover I/O or networking.
namespace cover_probe {
bool Store::busy() const noexcept { return false; }
bool Store::clear() noexcept { return true; }
bool Store::request(std::string_view) noexcept { return true; }
bool Store::request_file(std::string_view) noexcept { return true; }
void Store::step() noexcept {}
std::string_view Store::summary() const noexcept { return "COVER HOST TEST"; }
std::span<const std::uint8_t> Store::pixels() const noexcept { return {}; }
} // namespace cover_probe
namespace cover_fetch {
bool Fetcher::busy() const noexcept { return false; }
bool Fetcher::clear() noexcept { return true; }
bool Fetcher::request(std::string_view) noexcept { return true; }
void Fetcher::step() noexcept {}
std::string_view Fetcher::summary() const noexcept { return "DOWNLOAD HOST TEST"; }
} // namespace cover_fetch

int main() {
    stage = Stage::input; screen = Screen::library;
    user = 42; pad = 7; previous_buttons = 0;
    neutral_seen = false; status = "HOST CONTROL TEST"; last_button = "NO ACTION";
    directory_scan = {}; directory_scan.state = iso_probe::State::done; directory_scan.count = 1;
    constexpr std::string_view filename = "Ratchet & Clank 3.iso";
    directory_scan.names[0].length = filename.size();
    std::copy(filename.begin(), filename.end(), directory_scan.names[0].bytes.begin());
    serial_scan = {}; serial_scan.state = serial_probe::State::done;
    constexpr std::string_view serial = "SCUS-97353";
    std::copy(serial.begin(), serial.end(), serial_scan.serial.begin());
    constexpr std::string_view iso = "/data/PS2/isos/Ratchet & Clank 3.iso";
    std::copy(iso.begin(), iso.end(), serial_scan.path.begin());
    game_files = {}; transaction = {}; files.clear();
    files[apply_probe::master_path] = "--old-master=1\n";

    // Focus model: one game row, a settings button, then a launch button.
    release(); CHECK(neutral_seen && launch_calls == 0 && focus == 0);
    release(); CHECK(sample(ps5::pad::kButtonDown) && focus == 1);
    release(); CHECK(sample(ps5::pad::kButtonDown) && focus == 2);
    release(); CHECK(sample(ps5::pad::kButtonDown) && focus == 0);
    release(); CHECK(sample(ps5::pad::kButtonUp) && focus == 2);
    release(); CHECK(sample(ps5::pad::kButtonUp) && focus == 1);
    release(); CHECK(sample(ps5::pad::kButtonUp) && focus == 0);
    // Missing game files route to the prompt screen with no writes.
    CHECK(sample(ps5::pad::kButtonCross) && screen == Screen::library);
    finish_background(Screen::missing_files);
    CHECK(game_files.need_txt && game_files.need_lua && file_calls[1] == 0 && launch_calls == 0);
    // Recheck with nothing placed still reports missing.
    release(); CHECK(sample(ps5::pad::kButtonTriangle) && screen == Screen::missing_files);
    // Cancel leaves everything untouched (settle first so the press registers).
    drain_machines();
    release(); CHECK(sample(ps5::pad::kButtonCircle) && screen == Screen::library && file_calls[1] == 0);
    reseed_serial();
    // Back to missing, then create defaults.
    release(); CHECK(sample(ps5::pad::kButtonCross) && screen == Screen::library);
    finish_background(Screen::missing_files);
    release(); CHECK(sample(ps5::pad::kButtonCross) && screen == Screen::missing_files);
    finish_background(Screen::verifying_launch);
    finish_background(Screen::library);
    CHECK(last_button == "MASTER DIFFERS - PRESS LAUNCH TO APPLY");
    CHECK(files["/data/PS2/configs/SCUS-97353.txt"].find("--ps2-title-id=SCUS-97353\n") != std::string::npos);
    CHECK(files["/data/PS2/configs/SCUS-97353.lua"] == game_lua());
    CHECK(files[apply_probe::master_path] == "--old-master=1\n");
    // Settings screen: choose a backend, toggle options, then go back.
    release(); CHECK(sample(ps5::pad::kButtonDown) && focus == 1);
    release(); CHECK(sample(ps5::pad::kButtonCross) && screen == Screen::settings);
    release(); CHECK(sample(ps5::pad::kButtonDown) && settings_focus == 1);
    release(); CHECK(sample(ps5::pad::kButtonCross) && backend_choice == 1);
    release(); CHECK(sample(ps5::pad::kButtonUp) && settings_focus == 0);
    release(); CHECK(sample(ps5::pad::kButtonCross) && backend_choice == 0);
    for (std::size_t i = 0; i < kBackendCount; ++i) release(), CHECK(sample(ps5::pad::kButtonDown));
    CHECK(settings_focus == kBackendCount);
    release(); CHECK(sample(ps5::pad::kButtonCross) && cfg_opt[0]);
    for (int i = 0; i < 3; ++i) release(), CHECK(sample(ps5::pad::kButtonDown));
    CHECK(settings_focus == kBackendCount + 3);
    release(); CHECK(sample(ps5::pad::kButtonCross) && cfg_opt[3]);
    // Scanlines has no CLI flag and never toggles on.
    release(); CHECK(sample(ps5::pad::kButtonUp) && settings_focus == kBackendCount + 2);
    release(); CHECK(sample(ps5::pad::kButtonCross) && !cfg_opt[2]);
    for (int i = 0; i < 3; ++i) release(), CHECK(sample(ps5::pad::kButtonDown));
    CHECK(settings_focus == kBackendCount + 5);
    release(); CHECK(sample(ps5::pad::kButtonCross) && screen == Screen::library);
    // Launch from the library's launch button.
    release(); CHECK(sample(ps5::pad::kButtonDown) && focus == 2);
    release(); CHECK(sample(ps5::pad::kButtonCross) && screen == Screen::library);
    finish_background(Screen::confirm_privilege);
    CHECK(files[apply_probe::master_path].find("--host-display-mode=16:9\n") != std::string::npos);
    CHECK(files[apply_probe::master_path].find("--gs-progressive=1\n") != std::string::npos);
    CHECK(transaction.state == apply_probe::State::active_ready && launch_calls == 0);
    CHECK(sample(ps5::pad::kButtonIntercepted) && screen == Screen::applied && launch_calls == 0);
    release(); CHECK(sample(ps5::pad::kButtonR1) && screen == Screen::applied);
    finish_background(Screen::verifying_launch);
    finish_background(Screen::library);
    CHECK(last_button == "READY - PRESS LAUNCH");
    release(); CHECK(sample(ps5::pad::kButtonCross) && screen == Screen::library);
    finish_background(Screen::confirm_privilege);
    release(); CHECK(sample(ps5::pad::kButtonCross) && screen == Screen::privilege_wait && launch_calls == 0);
    CHECK(privilege_body == "{\"PID\":104}\n");
    privilege_uid = 0; privilege_exists = -1; privilege_error = ENOENT;
    finish_background(Screen::confirm_launch); release();
    launch_return = 0; CHECK(sample(ps5::pad::kButtonCross) && screen == Screen::launch_result);
    CHECK(launch_calls == 1 && launched_title == "LIBR12347" && launch_code == 0);
    CHECK(!sample(ps5::pad::kButtonCross) && launch_calls == 1);

    // Options back off so the fresh run compares the plain loader.
    cfg_opt[0] = false; cfg_opt[3] = false;

    // Fresh-run path verifies an already-active loader without any mutation.
    screen = Screen::library; game_files = {}; transaction = {}; privilege = {}; previous_buttons = 0; neutral_seen = false;
    focus = 0; backend_choice = 0;
    file_calls = {}; files[apply_probe::master_path] = active_config();
    files["/data/PS2/configs/SCUS-97353.txt"] = game_txt();
    files["/data/PS2/configs/SCUS-97353.lua"] = game_lua();
    release(); CHECK(sample(ps5::pad::kButtonR1) && screen == Screen::library);
    finish_background(Screen::verifying_launch);
    finish_background(Screen::library);
    CHECK(last_button == "READY - PRESS LAUNCH");
    CHECK(file_calls[1] == 0 && file_calls[3] == 0 && file_calls[4] == 0 && file_calls[6] == 0 && file_calls[7] == 0);
    probe_application_main();
}
