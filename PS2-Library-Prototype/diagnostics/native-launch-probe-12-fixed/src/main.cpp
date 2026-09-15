// PS2 Library: per-game file menu with loader-compatible HEN launch. SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_renderer.hpp"
#include "ps5_pad.hpp"
#include "iso_directory.hpp"
#include "serial_scan.hpp"
#include "gamefiles.hpp"
#include "apply_store.hpp"
#include "privilege_request.hpp"
#include "cover_store.hpp"
#include "cover_fetch.hpp"
#include "ui_icons.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

struct LaunchContext {
    std::uint32_t size, user, options;
    std::uint64_t crash_report;
    std::uint32_t check_flags;
};
static_assert(sizeof(LaunchContext) == 32);
static_assert(offsetof(LaunchContext, crash_report) == 16);
// Keep the known loader-compatible LncUtil import alongside the SystemService
// entry point. Probe 11 proved that this import pair is accepted by the active
// raw-ELF loader; Probe 12's failure came from dropping LncUtil entirely.
extern "C" int sceLncUtilLaunchApp(const char*, const char*[], LaunchContext*);
extern "C" int sceSystemServiceLaunchApp(const char*, char**, LaunchContext*);

// Keep the legacy import in the dynamic table without allocating a new data
// segment. The empty compiler barrier emits the relocation but has no runtime
// effect; the actual launch uses the SystemService entry point below.
static void retain_legacy_import() noexcept {
    const auto legacy = &sceLncUtilLaunchApp;
    asm volatile("" : : "r"(legacy) : "memory");
}

namespace {
enum class Stage { user_init, user_get, pad_init, pad_open, input, failed };
enum class Screen {
    library, settings, missing_files, applying, applied, verifying_launch,
    confirm_privilege, privilege_wait, privilege_failed,
    confirm_launch, launch_result, error
};
enum class Mode { none, verify, launch };
struct BackendChoice {
    std::string_view label;
    std::string_view title_id;
    std::string_view package_artifact;
    std::string_view status;
};

// A backend PKG contains one fixed emulator runtime.  These entries are the
// front-end's package registry: the selected title ID is what the launch call
// uses, while package_artifact is a human-readable build location shown in
// the menu.  The PS5 does not expose the Windows project path after install.
constexpr std::array<BackendChoice, 6> kBackends{{
    {"WOTM V1 EMPTY", "LIBR12347", "backends-zero-iso/PS2-WotM-v1-empty-LIBR12347.pkg", "BACKEND"},
    {"JAK2 V2 EMPTY", "LIBR12348", "backends-zero-iso/PS2-Jak2-v2-empty-LIBR12348.pkg", "BACKEND"},
    {"RECVX EMPTY", "LIBR12349", "backends-zero-iso/PS2-RECVX-empty-LIBR12349.pkg", "BACKEND"},
    {"RED FACTION EMPTY", "LIBR12350", "backends-zero-iso/PS2-RedFaction-empty-LIBR12350.pkg", "BACKEND"},
    {"KOF2000 EMPTY", "LIBR12351", "backends-zero-iso/PS2-KOF2000-empty-LIBR12351.pkg", "BACKEND"},
    {"WOTM V2 EMPTY", "LIBR12352", "backends-zero-iso/PS2-WotM-v2-empty-LIBR12352.pkg", "BACKEND"},
}};
constexpr std::size_t kBackendCount = kBackends.size();
Stage stage = Stage::user_init;
Screen screen = Screen::library;
std::array<std::int32_t, 4> codes{};
std::size_t completed = 0;
std::int32_t user = -1, pad = -1, read_error = 0, launch_code = 0;
std::size_t selected = 0;
std::uint32_t previous_buttons = 0;
bool neutral_seen = false, launch_called = false;
#ifdef PS2_LIBRARY_HOST_TEST
// The host controller test keeps the explicit gates so it can exercise them.
constexpr bool kFastLaunch = false;
#else
// The console build collapses the read/check/apply/jailbreak/launch flow.
constexpr bool kFastLaunch = true;
#endif
bool fast_path = false, fast_apply_fallback = false;
Mode launch_mode = Mode::none;
Mode pending_mode = Mode::none;
std::size_t backend_choice = 0;
std::size_t focus = 0;
std::size_t settings_focus = 0;
std::size_t list_first = 0;
bool cfg_opt[5] = {false, false, false, false, false};
constexpr std::array<std::string_view, 5> kCfgLabels{{
    "WIDESCREEN 16:9",
    "UPSCALING 2X / EDGESMOOTH",
    "SCANLINES",
    "PROGRESSIVE SCAN",
    "BILINEAR FILTER",
}};
// Only indices 0, 1, 3, 4 have verified backend syntax and emit lines.
// Scanlines has no CLI flag anywhere (wiki snapshot confirms); its row stays
// a visible N/A so nobody mistakes it for functional.
constexpr bool kCfgEmits[5] = {true, true, false, true, true};
std::uint32_t loading_tick = 0;
std::string_view status = "DISPLAY READY - CONTROLLER TEST STARTING";
std::string_view last_button = "NO BUTTON PRESSED";
iso_probe::Directory directory_scan;
serial_probe::Scanner serial_scan;
gamefiles::Files game_files;
apply_probe::Transaction transaction;
privilege_probe::Broker privilege;
cover_probe::Store cover;
cover_fetch::Fetcher cover_download;
std::array<char, cover_probe::max_serial + 1> cover_serial{};
bool cover_fetch_attempted = false;
// Loader master text (per-game files are keyed by game serial).
std::array<char, 512> loader_text{};
std::size_t loader_size = 0;
// Games window: the list scrolls so more than a screenful of ISOs is reachable.
constexpr std::size_t kGameRowsVisible = 8;
constexpr unsigned kGameRowHeight = 76;

std::array<char, 11> hex_code(std::int32_t value) noexcept {
    std::array<char, 11> text{'0', 'X'}; constexpr char digits[] = "0123456789ABCDEF";
    const auto bits = static_cast<std::uint32_t>(value);
    for (unsigned i = 0; i < 8; ++i) text[2 + i] = digits[(bits >> (28 - 4 * i)) & 15];
    return text;
}
// Color is stored 0xAABBGGRR (matches the tiled RGBA8 buffer), so build
// palette entries from RGB instead of hand-writing byte-swapped literals.
constexpr ps5::demo::Color rgb(unsigned r, unsigned g, unsigned b) noexcept {
    return static_cast<ps5::demo::Color>(UINT32_C(0xff000000) |
                                         (static_cast<std::uint32_t>(b) << 16) |
                                         (static_cast<std::uint32_t>(g) << 8) |
                                         static_cast<std::uint32_t>(r));
}
// Hand-rolled search: std::string_view::find pulls in memchr, which the
// import gate forbids.
bool contains(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > haystack.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        std::size_t j = 0;
        while (j < needle.size() && haystack[i + j] == needle[j]) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}
bool status_is_warning(std::string_view text) noexcept {
    constexpr std::string_view words[] = {"FAIL", "ERROR", "CANNOT", "NEEDS", "REJECT",
                                          "INVALID", "MISSING", "UNAVAILABLE", "BAD", "ERR"};
    for (const auto word : words)
        if (contains(text, word))
            return true;
    return false;
}
std::array<char, 12> decimal(std::size_t value) noexcept {
    std::array<char, 12> text{}, reversed{}; std::size_t length = 0;
    do { reversed[length++] = static_cast<char>('0' + value % 10); value /= 10; }
    while (value && length < reversed.size() - 1);
    for (std::size_t i = 0; i < length; ++i) text[i] = reversed[length - i - 1];
    return text;
}
bool set_status(std::string_view value) noexcept {
    if (status == value) return false;
    status = value; return true;
}
void clear_selection_result() noexcept {
    serial_scan.clear(); game_files.clear(); transaction.clear();
    privilege = {};
    cover.clear(); cover_download.clear(); cover_serial[0] = 0; cover_fetch_attempted = false;
    launch_called = false; launch_code = 0;
    fast_path = false; fast_apply_fallback = false;
    launch_mode = Mode::none; pending_mode = Mode::none; loader_size = 0;
}
void launch_backend() noexcept {
    LaunchContext context{}; context.size = sizeof(context); context.user = static_cast<std::uint32_t>(user);
    char* arguments[] = {nullptr}; launch_called = true;
    launch_code = sceSystemServiceLaunchApp(kBackends[backend_choice].title_id.data(), arguments, &context);
    screen = Screen::launch_result;
    last_button = launch_code < 0 ? "LAUNCH REQUEST FAILED - MASTER REMAINS APPLIED" :
                                    "LAUNCH REQUEST ACCEPTED - WAITING FOR APP SWITCH";
}

bool request_privilege_and_maybe_launch() noexcept {
    privilege = {};
    if (!privilege.start()) {
        screen = Screen::privilege_failed;
        last_button = "HEN REQUEST FAILED - SEE RESULT";
        return false;
    }
    if (privilege.state == privilege_probe::State::ready) {
        // Request consumption is the reliable signal on this setup.  The
        // UID syscall may continue to report 1, so never strand launch on a
        // UID-zero-only gate.
        if (kFastLaunch && fast_path) launch_backend();
        else { screen = Screen::confirm_launch; last_button = "REQUEST CONSUMED - LAUNCH RETRY READY"; }
        return true;
    }
    screen = Screen::privilege_wait;
    last_button = "READABLE REQUEST PUBLISHED - WAITING FOR CONSUME";
    return true;
}

// Checked-box overrides ride after the --config line, so they win over the
// game file deterministically without editing it. Scanlines (index 2) has no
// backend CLI flag and never emits.
bool append_overrides() noexcept {
    std::size_t at = loader_size;
    auto put = [&](std::string_view v) noexcept {
        if (at + v.size() + 1 > loader_text.size()) return false;
        for (char c : v) loader_text[at++] = c;
        loader_text[at] = 0; return true;
    };
    if (cfg_opt[0] && !put("--host-display-mode=16:9\n")) return false;
    if (cfg_opt[1] && !put("--gs-uprender=2x2\n--gs-upscale=edgesmooth\n")) return false;
    if (cfg_opt[3] && !put("--gs-progressive=1\n")) return false;
    if (cfg_opt[4] && !put("--gs-force-bilinear=1\n")) return false;
    loader_size = at;
    return true;
}
bool prepare_loader() noexcept {
    loader_size = 0;
    if (!gamefiles::Files::build_loader({serial_scan.serial.data()}, loader_text.data(), loader_text.size(), loader_size) ||
        !append_overrides()) {
        screen = Screen::error; last_button = "LOADER BUILD FAILED"; return false;
    }
    return true;
}
// Game-files arrival: compare only, never writes.
bool start_loader_check() noexcept {
    if (!prepare_loader()) return false;
    if (!transaction.request_loader_check(loader_text.data(), loader_size, serial_scan.serial.data())) {
        screen = Screen::error; last_button = "LOADER CHECK REQUEST REJECTED"; return false;
    }
    screen = Screen::verifying_launch;
    last_button = "CHECKING ACTIVE MASTER - READ ONLY";
    return true;
}

bool begin_game_files(Mode mode) noexcept {
    launch_mode = Mode::none;
    if (!game_files.clear() || !transaction.clear()) {
        screen = Screen::error; last_button = "RESULTS BUSY - WAIT AND RETRY"; return false;
    }
    privilege = {};
    launch_called = false; launch_code = 0; fast_path = kFastLaunch; fast_apply_fallback = false;
    if (!game_files.request(serial_scan.path.data(), serial_scan.serial.data())) {
        screen = Screen::error; last_button = "GAME FILES REQUEST REJECTED"; return false;
    }
    launch_mode = mode;
    last_button = "GAME FILES CHECK REQUESTED - READ ONLY";
    return true;
}

std::string_view cstr_view(const char* s, std::size_t cap) noexcept {
    std::size_t n = 0;
    while (n < cap && s[n] != 0) ++n;
    return {s, n};
}

// Presentation-only word wrap into at most two lines of max_cols characters,
// so a long ISO name stays inside its row instead of running past the box.
struct WrappedName {
    std::array<char, 128> first{};
    std::array<char, 128> second{};
    std::size_t first_length = 0;
    std::size_t second_length = 0;
    bool has_second() const noexcept { return second_length != 0; }
};

bool scan_path_equals(std::size_t index) noexcept {
    if (index >= directory_scan.count) return false;
    const auto name = directory_scan.names[index].view();
    constexpr std::string_view dir = "/data/PS2/isos/";
    const auto path = cstr_view(serial_scan.path.data(), serial_scan.path.size());
    if (path.size() != dir.size() + name.size()) return false;
    for (std::size_t i = 0; i < dir.size(); ++i) if (path[i] != dir[i]) return false;
    for (std::size_t i = 0; i < name.size(); ++i) if (path[dir.size() + i] != name[i]) return false;
    return true;
}
bool serial_matches_selected() noexcept {
    return serial_scan.state == serial_probe::State::done && scan_path_equals(selected);
}
// The cover follows the highlighted game, not the confirmed selection, so
// browsing with the D-pad previews each game's art without pressing X.
bool focus_scan_matches() noexcept {
    return serial_scan.state == serial_probe::State::done &&
        focus < directory_scan.count && scan_path_equals(focus);
}

bool cover_serial_matches(std::string_view serial) noexcept {
    if (cover_serial[0] == 0) return false;
    if (serial.size() >= cover_serial.size()) return false;
    for (std::size_t i = 0; i < serial.size(); ++i) if (cover_serial[i] != serial[i]) return false;
    return cover_serial[serial.size()] == 0;
}

// Advances the cover machines and, when the highlighted game's serial is
// known, loads (or downloads) that game's art. Returns true on any change so
// the caller can request a redraw. The local JPEG is the cache: a decode
// failure or missing file triggers one download attempt, after which a manual
// retry (Square) is required.
bool step_cover() noexcept {
    if (!focus_scan_matches()) {
        // Unknown serial yet (or focus is on a button): keep any in-flight
        // load finishing, otherwise leave the current cover as-is.
        if (cover.busy()) { cover.step(); return true; }
        if (cover_download.busy()) { cover_download.step(); return true; }
        return false;
    }
    const auto serial = cstr_view(serial_scan.serial.data(), serial_scan.serial.size());
    if (!cover_serial_matches(serial)) {
        std::size_t at = 0;
        for (const char c : serial) { if (at + 1 < cover_serial.size()) cover_serial[at++] = c; }
        cover_serial[at] = 0;
        cover.clear(); cover_download.clear(); cover_fetch_attempted = false;
        cover.request(serial);
        return true;
    }
    if (cover.busy()) { cover.step(); return true; }
    if (cover_download.busy()) { cover_download.step(); return true; }
    if (cover_download.state == cover_fetch::State::done && cover.state != cover_probe::State::done) {
        cover.clear(); cover.request(serial); return true;
    }
    if (cover.state == cover_probe::State::missing && !cover_fetch_attempted &&
        cover_download.state == cover_fetch::State::idle) {
        cover_fetch_attempted = true;
        cover_download.request(serial);
        return true;
    }
    return false;
}

// Read the highlighted game's disc ID in the background so its cover can be
// shown before any confirmation. Skipped while a launch/apply is in flight or
// when the same path already failed (avoids a rescan loop on a bad ISO).
void maybe_scan_focused_game() noexcept {
    if (screen != Screen::library || focus >= directory_scan.count) return;
    if (serial_scan.busy() || pending_mode != Mode::none) return;
    if (game_files.busy() || transaction.busy() || privilege.state != privilege_probe::State::idle) return;
    if (scan_path_equals(focus)) {
        if (serial_scan.state != serial_probe::State::done) return; // same path already failed
        return;
    }
    (void)serial_scan.request(directory_scan.names[focus].view());
}

bool begin_launch_flow(Mode mode) noexcept {
    if (!directory_scan.count || selected >= directory_scan.count) {
        last_button = "SELECT AN ISO BEFORE LAUNCH";
        return false;
    }
    // A completed scan only counts for the game it ran on; anything else
    // rescans instead of silently launching a stale selection.
    if (serial_matches_selected()) return begin_game_files(mode);
    clear_selection_result();
    fast_path = kFastLaunch; pending_mode = mode;
    if (serial_scan.request(directory_scan.names[selected].view())) {
        last_button = "GAME ID CHECK REQUESTED - READ ONLY";
        return true;
    }
    last_button = "READINESS CHECK UNAVAILABLE - INVALID NAME";
    return false;
}
// Library rows: games, then the settings button, then the launch button.
std::size_t row_total() noexcept { return directory_scan.count + 2; }
bool focus_is_game() noexcept { return focus < directory_scan.count; }
bool focus_is_settings_button() noexcept { return focus == directory_scan.count; }
bool focus_is_launch_button() noexcept { return focus == directory_scan.count + 1; }
// Settings rows: backends, then config checkboxes, then the back button.
std::size_t settings_total() noexcept { return kBackendCount + 5 + 1; }
bool settings_is_backend() noexcept { return settings_focus < kBackendCount; }
bool settings_is_cfg() noexcept {
    return settings_focus >= kBackendCount && settings_focus < kBackendCount + 5;
}
bool settings_is_back() noexcept { return settings_focus == kBackendCount + 5; }
// Keep the highlighted game inside the visible window.
void scroll_to_focus() noexcept {
    if (!focus_is_game()) return;
    if (focus < list_first) list_first = focus;
    else if (focus >= list_first + kGameRowsVisible) list_first = focus - kGameRowsVisible + 1;
    if (directory_scan.count && list_first > directory_scan.count - 1)
        list_first = directory_scan.count - 1;
}

bool update() noexcept {
    using namespace ps5::pad;
    ++loading_tick;
    switch (stage) {
    case Stage::user_init:
        codes[0] = sceUserServiceInitialize(nullptr); completed = 1; stage = Stage::user_get; return true;
    case Stage::user_get:
        codes[1] = sceUserServiceGetInitialUser(&user); completed = 2;
        stage = codes[1] < 0 ? Stage::failed : Stage::pad_init;
        if (stage == Stage::failed) status = "GET INITIAL USER FAILED";
        return true;
    case Stage::pad_init:
        codes[2] = scePadInit(); completed = 3;
        stage = codes[2] < 0 ? Stage::failed : Stage::pad_open;
        if (stage == Stage::failed) status = "PAD INITIALIZATION FAILED";
        return true;
    case Stage::pad_open:
        pad = scePadOpen(user, kPortTypeStandard, 0, nullptr); codes[3] = pad; completed = 4;
        stage = pad < 0 ? Stage::failed : Stage::input;
        status = pad < 0 ? "PAD OPEN FAILED" : "RELEASE ALL BUTTONS TO ENABLE INPUT";
        if (pad >= 0 && directory_scan.request()) last_button = "STARTUP SCAN REQUESTED - READ ONLY";
        return true;
    case Stage::failed: return false;
    case Stage::input: break;
    }
    bool background_changed = false;
    if (directory_scan.busy()) {
        directory_scan.step(); previous_buttons = 0; neutral_seen = false; return true;
    }
    if (serial_scan.busy()) {
        // A highlighted-game scan is a background preview: it must not reset
        // the input re-arm, or holding the D-pad to scroll would stall.
        const bool for_launch = pending_mode != Mode::none;
        serial_scan.step();
        if (serial_scan.state == serial_probe::State::done && pending_mode != Mode::none) {
            const Mode mode = pending_mode;
            pending_mode = Mode::none;
            if (begin_game_files(mode)) last_button = "GAME ID FOUND - FILES CHECK STARTED";
        }
        if (for_launch) { previous_buttons = 0; neutral_seen = false; return true; }
        background_changed = true; // background preview scan: keep reading input
    }
    if (game_files.busy()) {
        game_files.step();
        if (game_files.state == gamefiles::State::done) {
            if (!start_loader_check()) { previous_buttons = 0; neutral_seen = false; return true; }
        } else if (game_files.state == gamefiles::State::missing) {
            screen = Screen::missing_files;
        } else if (game_files.state == gamefiles::State::failed) {
            screen = Screen::error;
        }
        previous_buttons = 0; neutral_seen = false; return true;
    }
    if (transaction.state == apply_probe::State::failed &&
        transaction.failure == apply_probe::Failure::active_mismatch &&
        (screen == Screen::verifying_launch || screen == Screen::applying)) {
        if (launch_mode == Mode::launch && !fast_apply_fallback) {
            if (!prepare_loader() ||
                !transaction.request_loader_replace(loader_text.data(), loader_size, serial_scan.serial.data())) {
                screen = Screen::error; last_button = "AUTOMATIC APPLY REQUEST REJECTED";
            } else {
                fast_apply_fallback = true; screen = Screen::applying;
                last_button = "ACTIVE MASTER DIFFERED - APPLYING GAME LOADER";
            }
        } else {
            (void)transaction.clear();
            screen = Screen::library; last_button = "MASTER DIFFERS - PRESS LAUNCH TO APPLY";
        }
        previous_buttons = 0; neutral_seen = false; return true;
    }
    if (transaction.state == apply_probe::State::failed &&
        (screen == Screen::verifying_launch || screen == Screen::applying)) {
        screen = Screen::error;
        last_button = "FAST TRANSACTION FAILED - SEE DETAIL BEFORE RETRY";
        previous_buttons = 0; neutral_seen = false; return true;
    }
    if (transaction.busy()) {
        transaction.step();
        if (transaction.state == apply_probe::State::done) {
            if (transaction.request_active_check()) {
                screen = Screen::verifying_launch;
                last_button = "FAST LAUNCH - FINAL MASTER CHECK STARTED";
            } else screen = Screen::applied;
        } else if (transaction.state == apply_probe::State::active_ready) {
            if (launch_mode == Mode::launch) {
                if (kFastLaunch && fast_path) (void)request_privilege_and_maybe_launch();
                else screen = Screen::confirm_privilege;
            } else { screen = Screen::library; last_button = "READY - PRESS LAUNCH"; }
        }
        previous_buttons = 0; neutral_seen = false; return true;
    }
    if (privilege.state == privilege_probe::State::waiting) {
        privilege.step();
        if (privilege.state == privilege_probe::State::ready) {
            if (kFastLaunch && fast_path) launch_backend();
            else { screen = Screen::confirm_launch; last_button = "REQUEST CONSUMED - LAUNCH RETRY READY"; }
        }
        else if (privilege.state == privilege_probe::State::failed) screen = Screen::privilege_failed;
        previous_buttons = 0; neutral_seen = false; return true;
    }

    // Preview the highlighted game's disc ID and cover. Both advance one step
    // per frame and never block input: a state change is folded into the
    // redraw flag rather than short-circuiting the pad read (the host control
    // test depends on the input re-arm gating staying intact).
    maybe_scan_focused_game();
    background_changed = step_cover() || background_changed;

    std::array<Data, kMaxSamples> samples{};
    const int count = scePadRead(pad, samples.data(), static_cast<std::int32_t>(samples.size()));
    if (count < 0 || count > static_cast<int>(samples.size())) {
        previous_buttons = 0; neutral_seen = false; const bool changed = read_error != count; read_error = count;
        return set_status("PAD READ ERROR - SEND A PHOTO OF THIS SCREEN") || changed || background_changed;
    }
    if (count == 0) return background_changed;
    const bool recovered = read_error != 0; read_error = 0;
    const Data* latest = &samples[0];
    for (int i = 1; i < count; ++i) if (samples[i].timestamp_us > latest->timestamp_us) latest = &samples[i];
    if (!is_usable(*latest)) {
        previous_buttons = 0; neutral_seen = false;
        if (screen == Screen::missing_files ||
            screen == Screen::confirm_privilege || screen == Screen::confirm_launch) {
            if (screen == Screen::missing_files) (void)game_files.clear();
            screen = screen == Screen::missing_files ? Screen::library : Screen::applied;
            last_button = "INPUT INTERRUPTED - CONFIRMATION CANCELLED"; return true;
        }
        return set_status("INPUT DISCONNECTED OR INTERCEPTED - WAITING") || recovered || background_changed;
    }
    const auto buttons = latest->buttons;
    if (!neutral_seen) {
        previous_buttons = buttons;
        if (buttons == 0) { neutral_seen = true; return set_status("CONTROLLER READY") || recovered || background_changed; }
        return set_status("RELEASE ALL BUTTONS TO ENABLE INPUT") || recovered || background_changed;
    }
    const auto pressed = buttons & ~previous_buttons; previous_buttons = buttons;
    if (pressed == 0) return recovered || background_changed;

    if (pressed & kButtonCircle) {
        if (screen == Screen::missing_files) {
            (void)game_files.clear();
            screen = Screen::library; last_button = "MISSING CANCELLED - NO FILES WRITTEN";
        } else if (screen == Screen::confirm_privilege || screen == Screen::privilege_failed ||
                 screen == Screen::confirm_launch || screen == Screen::launch_result) {
            screen = Screen::confirm_privilege; last_button = "LAUNCH STEP CANCELLED - MASTER REMAINS APPLIED";
        } else if (screen == Screen::error) {
            if (transaction.clear() && game_files.clear()) { screen = Screen::library; last_button = "SAFE FAILURE CLEARED - REVIEW BEFORE RETRY"; }
            else last_button = "RESTART REQUIRED - FAILURE CANNOT BE CLEARED SAFELY";
        } else if (screen == Screen::settings) {
            screen = Screen::library; last_button = "BACK TO GAMES";
        } else if (screen == Screen::library) {
            focus = 0; list_first = 0;
            if (directory_scan.count) selected = 0;
            clear_selection_result(); last_button = "BACK TO GAMES";
        } else last_button = "CIRCLE RECEIVED - NO ACTION";
        return true;
    }
    if (screen == Screen::library) {
        const std::size_t total = row_total();
        if (pressed & kButtonUp) {
            if (total) focus = (focus + total - 1) % total;
            scroll_to_focus();
            return true;
        }
        if (pressed & kButtonDown) {
            if (total) focus = (focus + total + 1) % total;
            scroll_to_focus();
            return true;
        }
        // Right moves from the settings button across to the launch button.
        if ((pressed & kButtonRight) && focus_is_settings_button()) {
            focus = directory_scan.count + 1;
            return true;
        }
        if (pressed & kButtonTriangle) {
            if (directory_scan.request()) {
                selected = 0; focus = 0; list_first = 0;
                clear_selection_result(); last_button = "SCAN REQUESTED - READ ONLY";
            } else last_button = "SCAN UNAVAILABLE - CLOSE APP BEFORE RETRY";
            return true;
        }
        if (pressed & kButtonSquare) {
            const auto serial = cstr_view(serial_scan.serial.data(), serial_scan.serial.size());
            if (serial_matches_selected() && !serial.empty() &&
                (cover.state == cover_probe::State::missing || cover.state == cover_probe::State::failed)) {
                cover.clear(); cover_download.clear(); cover_fetch_attempted = true;
                cover_download.request(serial);
                last_button = "COVER DOWNLOAD RETRY REQUESTED";
            } else {
                last_button = "COVER RETRY NEEDS A GAME ID (PRESS X)";
            }
            return true;
        }
        if (pressed & kButtonCross) {
            if (focus_is_game()) {
                // Explicit select only: passing through a row while
                // navigating must never steal the selection (it once
                // launched the wrong game on the way to LAUNCH).
                selected = focus;
                (void)begin_launch_flow(Mode::verify);
            } else if (focus_is_settings_button()) {
                screen = Screen::settings; settings_focus = 0;
                last_button = "SETTINGS - SELECT EMULATOR OR EDIT CONFIGS";
            } else if (focus_is_launch_button()) {
                if (!directory_scan.count || selected >= directory_scan.count) {
                    last_button = "LAUNCH NEEDS A GAME - PICK ONE ABOVE";
                } else (void)begin_launch_flow(Mode::launch);
            } else last_button = "NOTHING FOCUSED";
            return true;
        } else if (pressed & kButtonR1) {
            if (!serial_matches_selected()) {
                last_button = "R1 NEEDS A GAME ID - PRESS X FIRST";
            } else if (begin_game_files(Mode::verify)) {
                last_button = "RECHECK REQUESTED - READ ONLY";
            }
            return true;
        } else {
            last_button = "UP DOWN MOVE / X ACT / TRIANGLE RESCAN";
        }
        return true;
    } else if (screen == Screen::settings) {
        const std::size_t total = settings_total();
        if (pressed & kButtonUp) {
            if (total) settings_focus = (settings_focus + total - 1) % total;
            return true;
        }
        if (pressed & kButtonDown) {
            if (total) settings_focus = (settings_focus + total + 1) % total;
            return true;
        }
        if (pressed & kButtonCross) {
            if (settings_is_backend()) {
                backend_choice = settings_focus;
                last_button = "BACKEND SELECTED";
            } else if (settings_is_cfg()) {
                const std::size_t index = settings_focus - kBackendCount;
                if (!kCfgEmits[index]) last_button = "SCANLINES HAS NO CLI FLAG - SKIPPED";
                else {
                    cfg_opt[index] = !cfg_opt[index];
                    last_button = cfg_opt[index] ? "OPTION ON - APPLIES AT LAUNCH" : "OPTION OFF";
                }
            } else {
                screen = Screen::library; last_button = "BACK TO GAMES";
            }
            return true;
        }
        last_button = "UP DOWN MOVE / X SELECT / CIRCLE BACK";
        return true;
    } else if (screen == Screen::missing_files && (pressed & kButtonCross)) {
        if (game_files.create_missing()) last_button = "CREATING DEFAULT GAME FILES";
        else { screen = Screen::error; last_button = "DEFAULT CREATE REJECTED"; }
        return true;
    } else if (screen == Screen::missing_files && (pressed & kButtonTriangle)) {
        if (!game_files.clear()) { screen = Screen::error; last_button = "RESULTS BUSY - WAIT AND RETRY"; }
        else if (!begin_launch_flow(Mode::verify)) { /* screen already set by begin_launch_flow */ }
        else last_button = "RECHECK REQUESTED - READ ONLY";
        return true;
    } else if (screen == Screen::applied && (pressed & kButtonR1)) {
        if (!serial_matches_selected()) {
            last_button = "R1 NEEDS A GAME ID - PRESS X FIRST";
        } else if (begin_game_files(Mode::verify)) {
            last_button = "RECHECK REQUESTED - READ ONLY";
        }
        return true;
    } else if (screen == Screen::confirm_privilege && (pressed & kButtonCross)) {
        (void)request_privilege_and_maybe_launch();
        return true;
    } else if (screen == Screen::confirm_launch && (pressed & kButtonCross)) {
        launch_backend(); return true;
    }
    last_button = "BUTTON NOT USED ON THIS SCREEN"; return true;
}

std::string_view screen_summary() noexcept {
    switch (screen) {
    case Screen::missing_files: return "GAME FILES MISSING - PLACE OR CREATE";
    case Screen::applying: return transaction.summary();
    case Screen::applied: return "MASTER APPLIED AND VERIFIED - BACKUP PRESERVED";
    case Screen::verifying_launch: return transaction.summary();
    case Screen::confirm_privilege: return "MASTER VERIFIED - CONFIRM HEN PRIVILEGE REQUEST";
    case Screen::privilege_wait: return privilege.summary();
    case Screen::privilege_failed: return privilege.state == privilege_probe::State::failed ? privilege.summary() :
        "HEN PRIVILEGE REQUEST FAILED - SEE RESULT";
    case Screen::confirm_launch: return "REQUEST CONSUMED - CONFIRM RESOLVED SYSTEMSERVICE LAUNCH";
    case Screen::launch_result: return launch_code < 0 ? "BACKEND LAUNCH REQUEST FAILED" : "BACKEND LAUNCH REQUEST ACCEPTED";
    case Screen::error:
        if (game_files.failure != gamefiles::Failure::none) return game_files.summary();
        return transaction.summary();
    default:
        if (game_files.busy()) return game_files.summary();
        if (serial_scan.state != serial_probe::State::idle) return serial_scan.summary();
        return directory_scan.summary();
    }
}

bool loading_active() noexcept {
    return stage != Stage::input || directory_scan.busy() || serial_scan.busy() ||
        game_files.busy() || transaction.busy() || privilege.state == privilege_probe::State::waiting;
}

unsigned loading_percent() noexcept {
    unsigned percent = 0;
    if (stage != Stage::input) {
        percent = static_cast<unsigned>(completed) * 20U;
    } else if (directory_scan.busy()) {
        if (directory_scan.state == iso_probe::State::opening) percent = 10;
        else if (directory_scan.state == iso_probe::State::reading) {
            percent = 20U + static_cast<unsigned>(directory_scan.batches) * 14U;
        } else percent = 82;
    } else if (serial_scan.busy()) {
        percent = 28U + static_cast<unsigned>(serial_scan.operations > 160 ? 16 : serial_scan.operations / 10);
    } else if (game_files.busy()) {
        percent = 46U + static_cast<unsigned>(game_files.operations > 32 ? 16 : game_files.operations / 2);
    } else if (transaction.busy()) {
        percent = 62U + static_cast<unsigned>(transaction.operations > 132 ? 30 : transaction.operations / 4);
    } else if (privilege.state == privilege_probe::State::waiting) {
        percent = 78U + static_cast<unsigned>(privilege.polls > 400 ? 18 : privilege.polls / 20);
    }
    if (percent > 98) percent = 98;
    return percent;
}

// PS5-style determinate bar: a thin rounded track with a light fill and a
// sheen that travels along the completed portion.
void draw_loading_bar(ps5::demo::Canvas& canvas, unsigned y) noexcept {
    using ps5::demo::Color;
    constexpr auto track = rgb(34, 35, 44);
    constexpr auto fill = rgb(184, 185, 196);
    constexpr auto sheen = rgb(232, 233, 238);
    constexpr auto label = rgb(150, 152, 164);
    constexpr unsigned bar_x = 100, bar_w = 1600, bar_h = 10;
    const unsigned percent = loading_percent();
    canvas.round_rect(bar_x, y, bar_w, bar_h, bar_h / 2, track);
    unsigned filled = bar_w * percent / 100U;
    if (filled < bar_h) filled = bar_h;
    canvas.round_rect(bar_x, y, filled, bar_h, bar_h / 2, fill);
    if (filled > bar_h)
    {
        const unsigned sheen_w = 200 < filled ? 200 : filled;
        const unsigned travel = filled - sheen_w;
        const unsigned position = travel == 0 ? 0 : (loading_tick * 16U) % (travel + 1U);
        canvas.round_rect(bar_x + position, y, sheen_w, bar_h, bar_h / 2, sheen);
    }
    const auto percent_text = decimal(percent);
    canvas.text(1720, y - 7, percent_text.data(), 3, label);
    canvas.text(1790, y - 7, "%", 3, label);
}

void draw(ps5::demo::Canvas& canvas) noexcept {
    using ps5::demo::Color;
    // Orbis-ish palette: near-black surface, translucent-dark panels with a
    // faint light border, white focus ring, white primary button.
    constexpr auto muted = rgb(150, 152, 164);
    constexpr auto accent = rgb(255, 255, 255);
    constexpr auto panel = rgb(24, 25, 31);
    constexpr auto panel_border = rgb(50, 52, 62);
    constexpr auto row_focus_fill = rgb(35, 37, 46);
    constexpr auto primary_text = rgb(14, 14, 18);
    constexpr auto ring_dim = rgb(120, 122, 132);
    constexpr auto scroll_track = rgb(40, 42, 50);
    constexpr auto scroll_thumb = rgb(150, 152, 164);
    constexpr auto shimmer = rgb(30, 32, 40);
    canvas.clear(rgb(13, 14, 18));
    const auto selection_box = [&canvas](unsigned x, unsigned y, unsigned w,
                                         unsigned h) noexcept {
        canvas.round_frame(x, y, w, h, 14, 3, accent, row_focus_fill);
    };
    // One button-prompt unit: the white DualSense glyph plus a short label.
    constexpr unsigned icon_h = ui_icons::pixel_height;
    const auto hint = [&canvas](unsigned x, unsigned y, ui_icons::Id id,
                                std::string_view label) noexcept -> unsigned {
        canvas.icon(x, y, static_cast<unsigned>(id), Color::white);
        x += icon_h + 8;
        canvas.text(x, y + (icon_h - 14) / 2, label, 2, Color::white);
        return x + canvas.text_width(label, 2) + 24;
    };
    // Word-wrap a name to a pixel width using the real glyph advances.
    const auto wrap_px = [&canvas](std::string_view name, unsigned max_px,
                                   unsigned scale) noexcept -> WrappedName {
        WrappedName out{};
        unsigned line = 0;
        while (!name.empty())
        {
            std::size_t fit = 0;
            while (fit < name.size() &&
                   canvas.text_width(name.substr(0, fit + 1), scale) <= max_px)
                ++fit;
            if (fit == 0)
                fit = 1;
            std::size_t take = fit;
            if (fit < name.size())
            {
                const std::size_t space = name.rfind(' ', fit);
                if (space != std::string_view::npos && space > 0)
                    take = space;
            }
            const std::string_view piece = name.substr(0, take);
            auto &target = (line == 0) ? out.first : out.second;
            std::size_t at = 0;
            for (const char c : piece)
                if (at + 1 < target.size())
                    target[at++] = c;
            if (line == 0) { out.first_length = at; line = 1; }
            else { out.second_length = at; break; }
            name.remove_prefix(take);
            while (!name.empty() && name.front() == ' ')
                name.remove_prefix(1);
        }
        return out;
    };
    // Bottom-right status pill; warnings stand out in amber.
    const auto status_pill = [&canvas](std::string_view message) noexcept {
        constexpr unsigned pill_x = 1080, pill_y = 1010, pill_w = 740, pill_h = 40;
        const bool warn = status_is_warning(message);
        canvas.round_frame(pill_x, pill_y, pill_w, pill_h, 20, 1,
                           warn ? ps5::demo::Color::yellow : panel_border, panel);
        canvas.text(canvas.text_center_start(pill_x + pill_w / 2, message, 2), pill_y + 13, message, 2,
                    warn ? ps5::demo::Color::yellow : Color::white);
    };
    canvas.text(100, 42, "Twiso", 9, Color::white);
    canvas.text(103, 125, ".ISO PATH: /data/ps2/isos      CONFIG PATH: /data/ps2/configs", 4, muted);
    canvas.rectangle(100, 172, 1720, 2, panel_border);
    // Top-right credit and handles.
    {
        constexpr unsigned ih = ui_icons::pixel_height;
        constexpr std::string_view made_by = "MADE BY: SWORD";
        constexpr std::string_view discord_handle = "sword.pdf";
        constexpr std::string_view x_handle = "sword_pdf";
        const unsigned made_w = canvas.text_width(made_by, 2);
        canvas.text(1820 - made_w, 30, made_by, 2, muted);
        const unsigned discord_w = canvas.text_width(discord_handle, 2);
        const unsigned x_w = canvas.text_width(x_handle, 2);
        const unsigned total = ih + 16 + discord_w + 28 + ih + 8 + x_w;
        unsigned bx = 1820 - total;
        constexpr unsigned by = 60;
        canvas.icon(bx, by, static_cast<unsigned>(ui_icons::Id::discord), Color::white);
        bx += ih + 16;
        canvas.text(bx, by + (ih - 14) / 2, discord_handle, 2, Color::white);
        bx += discord_w + 28;
        canvas.icon(bx, by, static_cast<unsigned>(ui_icons::Id::x), Color::white);
        bx += ih + 8;
        canvas.text(bx, by + (ih - 14) / 2, x_handle, 2, Color::white);
    }

    if (screen == Screen::missing_files) {
        canvas.text(100, 245, "GAME FILES MISSING", 4, Color::white);
        const auto stem = cstr_view(serial_scan.serial.data(), serial_scan.serial.size());
        unsigned row = 0;
        if (game_files.need_txt) {
            char line[96]{}; std::size_t at = 0;
            for (char c : std::string_view{"MISSING "}) line[at++] = c;
            for (char c : stem) { if (at + 5 < sizeof(line)) line[at++] = c; }
            for (char c : std::string_view{".TXT"}) line[at++] = c;
            canvas.text(100, 320 + row * 40, {line, at}, 3, Color::yellow);
            ++row;
        }
        if (game_files.need_lua) {
            char line[96]{}; std::size_t at = 0;
            for (char c : std::string_view{"MISSING "}) line[at++] = c;
            for (char c : stem) { if (at + 5 < sizeof(line)) line[at++] = c; }
            for (char c : std::string_view{".LUA"}) line[at++] = c;
            canvas.text(100, 320 + row * 40, {line, at}, 3, Color::yellow);
            ++row;
        }
        canvas.text(100, 480, "PLACE IN /data/PS2/CONFIGS VIA FTP", 3, muted);
        {
            unsigned hx = 100;
            hx = hint(hx, 520, ui_icons::Id::cross, "CREATE DEFAULTS");
            hx = 100;
            hx = hint(hx, 568, ui_icons::Id::triangle, "RECHECK");
            hx = hint(hx, 568, ui_icons::Id::circle, "CANCEL");
        }
        canvas.text(100, 770, status, 3, muted);
        canvas.text(100, 810, last_button, 3, Color::white);
        return;
    }

    if (screen == Screen::settings) {
        canvas.text(100, 230, "SETTINGS", 3, Color::white);
        canvas.round_frame(100, 250, 1720, 640, 22, 2, panel_border, panel);
        canvas.rectangle(124, 274, 1668, 2, panel_border);
        canvas.text(130, 292, "EMULATOR BACKEND", 3, Color::white);
        for (std::size_t i = 0; i < kBackendCount; ++i) {
            const unsigned y = 326 + static_cast<unsigned>(i) * 52;
            const bool hot = settings_focus == i;
            canvas.rectangle(124, y, 1670, 44, panel);
            if (hot) selection_box(124, y, 1670, 44);
            if (backend_choice == i) canvas.round_rect(148, y + 12, 20, 20, 5, accent);
            canvas.text(190, y + 11, kBackends[i].label, 3, hot ? accent : Color::white);
            canvas.text(780, y + 15, kBackends[i].title_id, 2, muted);
            if (backend_choice == i) canvas.text(1560, y + 15, "ACTIVE", 2, accent);
        }
        canvas.text(130, 648, "GAME OPTIONS (CHECKED LINES APPEND AFTER --config)", 3, Color::white);
        for (std::size_t i = 0; i < 5; ++i) {
            const unsigned y = 680 + static_cast<unsigned>(i) * 38;
            const bool hot = settings_focus == kBackendCount + i;
            canvas.rectangle(124, y, 1670, 32, panel);
            if (hot) selection_box(124, y, 1670, 32);
            const auto box = cfg_opt[i] ? accent : muted;
            canvas.round_rect(150, y + 8, 16, 16, 4, box);
            if (!cfg_opt[i]) canvas.round_rect(154, y + 12, 8, 8, 2, panel);
            canvas.text(190, y + 5, kCfgLabels[i], 3, hot ? accent : Color::white);
            if (i == 2) canvas.text(1560, y + 8, "N/A", 2, muted);
            else canvas.text(1560, y + 8, cfg_opt[i] ? "ON" : "OFF", 2, box);
        }
        {
            const bool hot = settings_is_back();
            if (hot) canvas.round_frame(700, 900, 520, 70, 35, 3, accent, row_focus_fill);
            else canvas.round_frame(700, 900, 520, 70, 35, 2, panel_border, panel);
            const unsigned back_width = canvas.text_width("BACK", 5);
            canvas.text(960 - back_width / 2, 918, "BACK", 5, Color::white);
        }
        canvas.rectangle(100, 996, 1720, 2, panel);
        unsigned hx = 100;
        constexpr unsigned hy = 1014;
        hx = hint(hx, hy, ui_icons::Id::dpad_up, "MOVE");
        hx = hint(hx, hy, ui_icons::Id::cross, "SELECT");
        hx = hint(hx, hy, ui_icons::Id::circle, "BACK");
        status_pill(last_button);
        return;
    }

    if (screen == Screen::library) {
        // Left: a scrollable games box (long names wrap to a second line).
        // Right: the cover pane, bordered to the artwork's own aspect ratio.
        const bool have_serial = serial_scan.state == serial_probe::State::done;
        const auto serial = have_serial
            ? cstr_view(serial_scan.serial.data(), serial_scan.serial.size())
            : std::string_view{};
        constexpr unsigned games_x = 100, games_y = 220, games_w = 1100, games_h = 650;
        constexpr unsigned pane_y = 220, pane_h = 650, pane_x = 1290;
        canvas.text(games_x, 182, "GAMES", 4, Color::white);
        {
            const bool ready = game_files.state == gamefiles::State::done;
            const bool missing = game_files.state == gamefiles::State::missing;
            const bool failed = game_files.state == gamefiles::State::failed;
            auto word = [&](bool need) noexcept -> std::string_view {
                if (failed) return "ERR";
                if (ready) return "OK";
                if (missing) return need ? "NEW" : "OK";
                if (game_files.busy()) return "...";
                return "--";
            };
            canvas.text(700, 188, "TXT", 2, muted);
            canvas.text(760, 188, word(game_files.need_txt), 2,
                        failed || (missing && game_files.need_txt) ? Color::yellow : Color::white);
            canvas.text(900, 188, "LUA", 2, muted);
            canvas.text(960, 188, word(game_files.need_lua), 2,
                        failed || (missing && game_files.need_lua) ? Color::yellow : Color::white);
        }
        {
            const auto &backend = kBackends[backend_choice];
            std::array<char, 64> text{};
            std::size_t at = 0;
            auto put = [&](std::string_view s) noexcept { for (char c : s) if (at + 1 < text.size()) text[at++] = c; };
            put("BACKEND: "); put(backend.label); put("  "); put(backend.title_id);
            const std::string_view line{text.data(), at};
            canvas.text(1820 - canvas.text_width(line, 2), 125, line, 2, Color::white);
        }
        canvas.round_frame(games_x, games_y, games_w, games_h, 22, 2, panel_border, panel);
        const unsigned inner_left = games_x + 12;
        const unsigned inner_w = games_w - 24;
        const unsigned list_top = games_y + 12;
        for (std::size_t row = 0; row < kGameRowsVisible; ++row) {
            const std::size_t i = list_first + row;
            if (i >= directory_scan.count) break;
            const unsigned y = list_top + static_cast<unsigned>(row) * kGameRowHeight;
            const bool hot = focus == i;
            const bool chosen = selected == i;
            if (hot) selection_box(games_x + 8, y, games_w - 16, kGameRowHeight - 4);
            else if (chosen) canvas.round_frame(games_x + 8, y, games_w - 16, kGameRowHeight - 4, 14, 2, ring_dim, panel);
            std::array<char, 256> clean{}; iso_probe::display_name(directory_scan.names[i].view(), clean);
            const auto name = cstr_view(clean.data(), clean.size());
            const auto color = hot ? accent : (chosen ? Color::white : muted);
            if (canvas.text_width(name, 4) <= inner_w - 24) {
                canvas.text(canvas.text_center_start(inner_left + inner_w / 2, name, 4), y + 22, name, 4, color);
            } else {
                const auto wrapped = wrap_px(name, inner_w - 24, 4);
                const std::string_view first{wrapped.first.data(), wrapped.first_length};
                canvas.text(canvas.text_center_start(inner_left + inner_w / 2, first, 4), y + 8, first, 4, color);
                if (wrapped.has_second()) {
                    const std::string_view second{wrapped.second.data(), wrapped.second_length};
                    canvas.text(canvas.text_center_start(inner_left + inner_w / 2, second, 4), y + 40, second, 4, color);
                }
            }
            if (hot && !serial.empty()) {
                const unsigned sw = canvas.text_width(serial, 2);
                canvas.text(games_x + games_w - 72 - sw, y + 26, serial, 2, muted);
            }
        }
        if (directory_scan.count > kGameRowsVisible) {
            const unsigned track_x = games_x + games_w - 14;
            const unsigned track_y = list_top + 4;
            const unsigned track_h = kGameRowHeight * kGameRowsVisible - 24;
            canvas.round_rect(track_x, track_y, 4, track_h, 2, scroll_track);
            unsigned thumb_h = track_h * kGameRowsVisible / directory_scan.count;
            if (thumb_h < 28) thumb_h = 28;
            const unsigned max_first = directory_scan.count - kGameRowsVisible;
            const unsigned thumb_y = max_first ? track_y + (track_h - thumb_h) * list_first / max_first : track_y;
            canvas.round_rect(track_x, thumb_y, 4, thumb_h, 2, scroll_thumb);
        }
        if (!directory_scan.count) {
            constexpr std::string_view empty_title = "NO ISOs FOUND";
            constexpr std::string_view empty_hint = "ADD .ISO FILES TO /DATA/PS2/ISOS";
            canvas.text(canvas.text_center_start(games_x + games_w / 2, empty_title, 4), games_y + 250,
                        empty_title, 4, Color::white);
            canvas.text(canvas.text_center_start(games_x + games_w / 2, empty_hint, 2), games_y + 300,
                        empty_hint, 2, muted);
        }
        unsigned pane_w = static_cast<unsigned>(static_cast<std::uint64_t>(pane_h) * 512 / 736);
        const auto cover_pixels = cover.pixels();
        const bool cover_ready = !cover_pixels.empty() && cover.width > 0 && cover.height > 0;
        if (cover_ready)
            pane_w = static_cast<unsigned>(static_cast<std::uint64_t>(pane_h) * cover.width / cover.height);
        canvas.text(pane_x, 182, "COVER", 4, Color::white);
        if (!serial.empty()) canvas.text(pane_x + 250, 182, serial, 4, Color::white);
        else if (serial_scan.busy()) canvas.text(pane_x + 250, 182, "SCANNING", 4, Color::yellow);
        canvas.round_frame(pane_x, pane_y, pane_w, pane_h, 22, 3, panel_border, panel);
        if (cover_ready) {
            canvas.blit_rgba_round(pane_x + 3, pane_y + 3, pane_w - 6, pane_h - 6, 19, cover_pixels,
                                   static_cast<unsigned>(cover.width), static_cast<unsigned>(cover.height));
        } else if (cover.busy() || cover_download.busy()) {
            // Skeleton shimmer while the art is read/decoded/downloaded.
            const unsigned band_h = 48;
            const unsigned travel = pane_h > band_h ? pane_h - band_h : 0;
            const unsigned cycle = travel ? travel * 2 : 1;
            const unsigned step = (loading_tick * 9U) % cycle;
            const unsigned band_y = pane_y + (step < travel ? step : travel * 2 - step);
            canvas.round_rect(pane_x + 6, band_y, pane_w - 12, band_h, 12, shimmer);
            constexpr std::string_view loading = "LOADING COVER";
            canvas.text(canvas.text_center_start(pane_x + pane_w / 2, loading, 3),
                        pane_y + pane_h / 2, loading, 3, muted);
        } else {
            const auto summary = cover.summary();
            canvas.text(canvas.text_center_start(pane_x + pane_w / 2, summary, 3), pane_y + 250,
                        summary, 3, muted);
            if (cover_download.state == cover_fetch::State::failed) {
                const auto reason = cover_download.summary();
                canvas.text(canvas.text_center_start(pane_x + pane_w / 2, reason, 3), pane_y + 300,
                            reason, 3, Color::yellow);
            } else if (!cover_serial[0]) {
                hint(pane_x + 24, pane_y + 296, ui_icons::Id::cross, "SELECT A GAME");
            }
        }
        {
            const bool hot = focus_is_settings_button();
            if (hot) canvas.round_frame(100, 900, 820, 90, 45, 3, accent, row_focus_fill);
            else canvas.round_frame(100, 900, 820, 90, 45, 2, panel_border, panel);
            constexpr std::string_view label = "SELECT EMU / EDIT CONFIGS";
            canvas.text(canvas.text_center_start(510, label, 4), 931, label, 4, Color::white);
        }
        {
            const bool hot = focus_is_launch_button();
            if (hot) canvas.round_rect(980, 900, 840, 90, 45, Color::white);
            else canvas.round_frame(980, 900, 840, 90, 45, 2, panel_border, panel);
            constexpr std::string_view label = "LAUNCH";
            canvas.text(canvas.text_center_start(1400, label, 4), 931, label, 4,
                        hot ? primary_text : Color::white);
        }
        canvas.rectangle(100, 1000, 1720, 2, panel);
        unsigned hx = 100;
        constexpr unsigned hy = 1018;
        hx = hint(hx, hy, ui_icons::Id::dpad_up, "MOVE");
        hx = hint(hx, hy, ui_icons::Id::cross, "SELECT");
        hx = hint(hx, hy, ui_icons::Id::triangle, "RESCAN");
        hx = hint(hx, hy, ui_icons::Id::square, "COVER RETRY");
        hx = hint(hx, hy, ui_icons::Id::circle, "BACK");
        status_pill(last_button);
        return;
    }
    const bool bad = screen == Screen::error || screen == Screen::privilege_failed ||
        (screen == Screen::launch_result && launch_code < 0) ||
        (screen == Screen::library && serial_scan.state == serial_probe::State::failed);
    canvas.text(100, 625, screen_summary(), 3, bad ? Color::yellow : accent);
    if (transaction.state != apply_probe::State::idle) {
        canvas.text(100, 668, "STEP", 3, muted); canvas.text(220, 668, transaction.active_path.data(), 3, Color::white);
        canvas.text(650, 668, "OPS", 3, muted); const auto ops = decimal(transaction.operations);
        canvas.text(760, 668, ops.data(), 3, Color::white);
        if (transaction.backup_path[0] && privilege.state == privilege_probe::State::idle) {
            canvas.text(100, 708, "BACKUP", 3, muted); canvas.text(260, 708, transaction.backup_path.data(), 2, Color::white);
        }
        if (privilege.state != privilege_probe::State::idle) {
            canvas.text(100, 708, "PID", 3, muted); const auto pid_text = decimal(static_cast<std::size_t>(privilege.pid));
            canvas.text(220, 708, pid_text.data(), 3, Color::white);
            canvas.text(430, 708, "UID BEFORE", 3, muted); const auto before = decimal(static_cast<std::size_t>(privilege.uid_before < 0 ? 0 : privilege.uid_before));
            canvas.text(650, 708, before.data(), 3, Color::white);
            canvas.text(850, 708, "UID AFTER", 3, muted); const auto after = decimal(static_cast<std::size_t>(privilege.uid_after < 0 ? 0 : privilege.uid_after));
            canvas.text(1050, 708, after.data(), 3, privilege.uid_after == 0 ? accent : Color::yellow);
            canvas.text(100, 748, "MODE 0666", 3, muted); const auto mode_result = hex_code(privilege.permission_result);
            canvas.text(300, 748, std::string_view{mode_result.data(), 10}, 3, privilege.permission_result == 0 ? Color::white : Color::yellow);
            canvas.text(650, 748, "PUBLISH", 3, muted); const auto publish_result = hex_code(privilege.publish_result);
            canvas.text(850, 748, std::string_view{publish_result.data(), 10}, 3, privilege.publish_result == 0 ? Color::white : Color::yellow);
        }
        if (screen == Screen::launch_result && launch_called) {
            canvas.text(1200, 668, "RESULT", 3, muted); const auto result = hex_code(launch_code);
            canvas.text(1370, 668, std::string_view{result.data(), 10}, 3, launch_code < 0 ? Color::yellow : Color::white);
        }
    }
    // While a launch/apply is running, keep the game artwork visible with a
    // note when option lines are being applied alongside it.
    if ((screen == Screen::applying || screen == Screen::verifying_launch) && loading_active()) {
        const auto launch_pixels = cover.pixels();
        constexpr unsigned art_h = 300;
        const unsigned art_w = static_cast<unsigned>(static_cast<std::uint64_t>(art_h) * 512 / 736);
        const unsigned art_x = 1820 - art_w, art_y = 280;
        if (!launch_pixels.empty() && cover.width > 0 && cover.height > 0) {
            canvas.blit_rgba_round(art_x, art_y, art_w, art_h, 12, launch_pixels,
                                   static_cast<unsigned>(cover.width), static_cast<unsigned>(cover.height));
        } else {
            canvas.rectangle(art_x, art_y, art_w, art_h, panel);
        }
        bool any_config = false;
        for (std::size_t i = 0; i < 5; ++i) if (cfg_opt[i] && kCfgEmits[i]) any_config = true;
        canvas.text(art_x, art_y + art_h + 14, any_config ? "CONFIGS RUNNING" : "LOADING", 3,
                    any_config ? accent : Color::white);
    }
    if (screen != Screen::library && screen != Screen::missing_files && loading_active()) {
        draw_loading_bar(canvas, 800);
    } else {
        canvas.text(100, 770, status, 3, muted); canvas.text(100, 810, last_button, 3, Color::white);
    }
    if (read_error) {
        canvas.text(1200, 770, "PAD ERROR", 3, Color::yellow); const auto code = hex_code(read_error);
        canvas.text(1400, 770, std::string_view{code.data(), 10}, 3, Color::yellow);
    }
    canvas.rectangle(100, 890, 1720, 2, panel_border);
    {
        unsigned hx = 100;
        constexpr unsigned cy = 906;
        switch (screen) {
        case Screen::applying:
        case Screen::verifying_launch:
            canvas.text(hx, cy + 7, "WAIT - DO NOT CLOSE THE APP DURING THIS CHECK", 3, Color::white);
            break;
        case Screen::applied:
            hx = hint(hx, cy, ui_icons::Id::r1, "FINAL MASTER CHECK");
            hx = hint(hx, cy, ui_icons::Id::home, "EXIT");
            break;
        case Screen::confirm_privilege:
            hx = hint(hx, cy, ui_icons::Id::cross, "REQUEST HEN JAILBREAK");
            hx = hint(hx, cy, ui_icons::Id::circle, "CANCEL");
            break;
        case Screen::privilege_wait:
            canvas.text(hx, cy + 7, "WAIT - HEN REQUEST IS PENDING", 3, Color::white);
            break;
        case Screen::privilege_failed:
            hx = hint(hx, cy, ui_icons::Id::circle, "RETURN");
            canvas.text(hx, cy + 7, "SEND MODE, PUBLISH, UID, KLOG", 3, Color::white);
            break;
        case Screen::confirm_launch:
            hx = hint(hx, cy, ui_icons::Id::cross, "RETRY SYSTEMSERVICE LAUNCH");
            hx = hint(hx, cy, ui_icons::Id::circle, "CANCEL");
            break;
        case Screen::launch_result:
            hint(hx, cy, ui_icons::Id::circle, "RETURN TO APPLIED");
            break;
        case Screen::error:
            hint(hx, cy, ui_icons::Id::circle, "CLEAR SAFE FAILURES");
            break;
        default:
            break;
        }
    }
    canvas.text(100, 955, "PER-GAME FILES IN /data/PS2/CONFIGS AS <SERIAL>.TXT + .LUA", 3, muted);
}
} // namespace

int main() {
    retain_legacy_import();
    ps5::demo::run(draw, update, "");
}
