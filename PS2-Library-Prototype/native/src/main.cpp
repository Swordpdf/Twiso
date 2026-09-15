// PS2 Library prototype. SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_renderer.hpp"
#include "profile_store.hpp"
#include "ps5_pad.hpp"
#include <array>
#include <cstddef>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

// Public ABI: ps5-payload-dev/websrv, src/ps5/sys.c. No privilege or patch backend.
struct LaunchContext {
    std::uint32_t size, user, options;
    std::uint64_t crash_report;
    std::uint32_t check_flags;
};
static_assert(sizeof(LaunchContext) == 32);
static_assert(offsetof(LaunchContext, crash_report) == 16);
extern "C" int sceSystemServiceLaunchApp(const char*, char**, LaunchContext*);

namespace {
enum class Screen { library, confirm_apply, applied, confirm_launch, launch_result };
Screen screen = Screen::library;
std::size_t selected = 0;
int pad = -1;
std::int32_t user = -1;
std::uint32_t previous_buttons = 0;
bool have_neutral_input = false;
int pad_error = 0;
ps2lib::Result result{};
std::array<char, 160> detail{};

void log(const char* message, int code = 0) noexcept {
    // Only title-local diagnostics; no master writes on menu startup.
    const int fd = open("/download0/ps2-library.log", O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    std::array<char, 384> line{};
    const int length = std::snprintf(line.data(), line.size(), "%s | 0x%08X\n", message, static_cast<unsigned>(code));
    if (length > 0 && static_cast<std::size_t>(length) < line.size())
        (void)write(fd, line.data(), static_cast<std::size_t>(length));
    (void)close(fd);
}
void check_selected() noexcept {
    result = ps2lib::validate(ps2lib::profiles[selected]);
    std::snprintf(detail.data(), detail.size(), "FILE CHECK CODE 0x%08X", static_cast<unsigned>(result.error));
    log(result.message.data(), result.error);
}
void open_pad() noexcept {
    (void)sceUserServiceInitialize(nullptr);
    pad_error = sceUserServiceGetInitialUser(&user);
    if (pad_error >= 0) {
        pad_error = scePadInit();
        if (pad_error >= 0) {
            pad = scePadOpen(user, ps5::pad::kPortTypeStandard, 0, nullptr);
            if (pad < 0) pad_error = pad;
        }
    }
    log("CONTROLLER INITIALIZATION", pad_error);
}
bool update() noexcept {
    using namespace ps5::pad;
    if (pad < 0) return false;
    std::array<Data, kMaxSamples> samples{};
    const int count = scePadRead(pad, samples.data(), static_cast<std::int32_t>(samples.size()));
    if (count <= 0 || static_cast<std::size_t>(count) > samples.size()) return false;
    const Data* latest = &samples[0];
    for (int i = 1; i < count; ++i) if (samples[i].timestamp_us > latest->timestamp_us) latest = &samples[i];
    if (!is_usable(*latest)) {
        previous_buttons = 0; have_neutral_input = false;
        if (screen == Screen::confirm_apply || screen == Screen::confirm_launch) {
            screen = Screen::library;
            std::snprintf(detail.data(), detail.size(), "INPUT INTERRUPTED - CONFIRMATION CANCELLED");
            return true;
        }
        return false;
    }
    const auto buttons = latest->buttons;
    if (!have_neutral_input) {
        previous_buttons = buttons;
        if (buttons == 0) have_neutral_input = true;
        return false;
    }
    const auto pressed = buttons & ~previous_buttons;
    previous_buttons = buttons;
    if (pressed == 0) return false;
    if (pressed & kButtonCircle) {
        screen = Screen::library;
        std::snprintf(detail.data(), detail.size(), "CANCELLED - NO ADDITIONAL CHANGE");
        return true;
    }
    if (screen == Screen::library) {
        if (pressed & (kButtonUp | kButtonDown)) {
            selected = 1 - selected; result = {}; detail.fill(0); return true;
        }
        if (pressed & kButtonTriangle) { check_selected(); return true; }
        if (pressed & kButtonCross) {
            check_selected();
            if (result.ok) screen = Screen::confirm_apply;
            return true;
        }
    } else if (screen == Screen::confirm_apply && (pressed & kButtonCross)) {
        result = ps2lib::apply(ps2lib::profiles[selected]);
        screen = result.ok ? Screen::applied : Screen::library;
        log(result.message.data(), result.error);
        if (result.backup[0]) log(result.backup.data());
        std::snprintf(detail.data(), detail.size(), "WRITE CODE 0x%08X", static_cast<unsigned>(result.error));
        return true;
    } else if (screen == Screen::applied && (pressed & kButtonR1)) {
        screen = Screen::confirm_launch; return true;
    } else if (screen == Screen::confirm_launch && (pressed & kButtonCross)) {
        result = ps2lib::validate_active(ps2lib::profiles[selected]);
        if (!result.ok) {
            log(result.message.data(), result.error);
            detail.fill(0); screen = Screen::library; return true;
        }
        LaunchContext context{};
        context.size = sizeof(context); context.user = static_cast<std::uint32_t>(user);
        char* args[] = {nullptr};
        const char* target = ps2lib::profiles[selected].launcher_id;
        log(target);
        const int code = sceSystemServiceLaunchApp(target, args, &context);
        log("LAUNCH REQUEST RETURNED", code);
        result.ok = code >= 0; result.error = code;
        std::snprintf(result.message.data(), result.message.size(), "%s",
            code < 0 ? "LAUNCH REQUEST FAILED - PROFILE REMAINS SELECTED" :
                       "LAUNCH REQUEST SENT - GAME BOOT NOT YET VERIFIED");
        std::snprintf(detail.data(), detail.size(), "LAUNCH RESULT 0x%08X", static_cast<unsigned>(code));
        screen = Screen::launch_result; return true;
    }
    return false;
}
void draw(ps5::demo::Canvas& canvas) noexcept {
    using ps5::demo::Color;
    const auto bg = static_cast<Color>(0xff19130e);
    const auto panel = static_cast<Color>(0xff30271c);
    const auto muted = static_cast<Color>(0xffb1a18c);
    const auto accent = static_cast<Color>(0xffffd36c);
    canvas.clear(bg);
    canvas.text(100, 70, "PS2 LIBRARY", 11, Color::white);
    canvas.text(103, 174, "NATIVE MENU / TWO PROFILE TEST / PROTOTYPE 01", 4, muted);
    canvas.rectangle(100, 235, 1720, 3, accent);
    for (std::size_t i = 0; i < ps2lib::profiles.size(); ++i) {
        unsigned y = 280 + static_cast<unsigned>(i) * 175;
        canvas.rectangle(100, y, 1720, 145, panel);
        if (selected == i) canvas.rectangle(100, y, 8, 145, accent);
        canvas.text(140, y + 27, ps2lib::profiles[i].name, 7, selected == i ? accent : Color::white);
        canvas.text(144, y + 98, ps2lib::profiles[i].serial, 3, muted);
        canvas.text(610, y + 98, i == 0 ? "WOTM V1 / YOUR PROVEN CLI AND LUA" :
                                                   "WOTM V1 / NO GAME PATCHES", 3, muted);
    }
    if (pad < 0) {
        canvas.text(100, 655, "CONTROLLER INITIALIZATION FAILED", 4, Color::yellow);
        std::array<char, 64> error{};
        std::snprintf(error.data(), error.size(), "PAD RESULT 0x%08X", static_cast<unsigned>(pad_error));
        canvas.text(100, 710, error.data(), 4, Color::white);
    } else if (screen == Screen::confirm_apply) {
        canvas.text(100, 650, "IS THE PS2 APP FULLY CLOSED - NOT SUSPENDED?", 4, Color::yellow);
        canvas.text(100, 705, "CROSS CONFIRMS AND REPLACES MASTER WITH A BACKUP", 4, Color::white);
        canvas.text(100, 760, "CIRCLE CANCELS WITHOUT CHANGING THE CONFIG", 4, muted);
    } else if (screen == Screen::confirm_launch) {
        canvas.text(100, 650, "TEST AUTOMATIC LAUNCH OF TEST12345?", 4, Color::yellow);
        canvas.text(100, 705, "CROSS SENDS ONE NORMAL SYSTEM LAUNCH REQUEST", 4, Color::white);
        canvas.text(100, 760, "NO PROCESS KILLING OR SYSTEM PATCHING", 4, muted);
    } else {
        if (result.message[0]) canvas.text(100, 650, result.message.data(), 3, result.ok ? accent : Color::yellow);
        if (detail[0]) canvas.text(100, 705, detail.data(), 3, muted);
        canvas.text(100, 770, screen == Screen::applied || screen == Screen::launch_result ?
            "MANUAL FALLBACK - CLOSE THIS MENU THEN OPEN PS2 APP" :
            "OPENING OR BROWSING DOES NOT MODIFY MASTER", 3, muted);
    }
    canvas.rectangle(100, 864, 1720, 2, panel);
    canvas.text(100, 900, screen == Screen::applied ? "R1 TEST LAUNCH / CIRCLE BACK / PS BUTTON TO CLOSE" :
                       "UP DOWN SELECT / CROSS CONFIRM / TRIANGLE CHECK / CIRCLE BACK", 3, Color::white);
    canvas.text(100, 960, "KEEP YOUR DATA MOUNT ACTIVE - THIS MENU DOES NOT ENABLE IT", 3, muted);
}
}
int main() {
    log("PS2 LIBRARY 01.000.001 STARTING - MASTER UNCHANGED");
    open_pad();
    ps5::demo::run(draw, update, "PS2 Library prototype ready");
}
