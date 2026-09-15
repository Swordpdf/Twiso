// Host-only transactional activation tests. No PS5 runtime or app launch.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/apply_store.hpp"
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
struct OpenHandle { std::string* data; std::size_t pos; };
std::map<int, OpenHandle> open_files;
std::string active;
int next_fd = 20, fake_error = 0;
int max_read = 1 << 20, max_write = 1 << 20;
int fail_write = -1, fail_sync = -1, fail_close = -1, fail_rename = -1, fail_unlink = -1;
int oversize_read = -1, master_opens = 0, change_master_on_open = -1;
void check(bool ok, int line) {
    ++checks; if (!ok) { std::fprintf(stderr, "Apply check failed on line %d\n", line); std::abort(); }
}
#define CHECK(x) check((x), __LINE__)
std::string config() {
    return "# library fixture\n--path-vmc=\"/tmp/vmc\"\n"
        "--config-local-lua=\"/data/PS2/configs/rac3.lua\"\n"
        "--ps2-title-id=SCUS-97353\n--max-disc-num=1\n"
        "--image=\"/data/PS2/isos/Ratchet & Clank 3.iso\"\n"
        "--rom=\"PS20220WD20050620.crack\"\n"
        "--host-display-mode=4:3\n--gs-uprender=none\n--gs-upscale=none\n"
        "--ee-hook=1,A,,2\n--ee-hook=3,B,,4\n";
}
std::string applied_config() {
    auto value = config();
#if defined(PS2_LIBRARY_SHARED_VMC) && PS2_LIBRARY_SHARED_VMC
    const std::string from = "--path-vmc=\"/tmp/vmc\"";
    const std::string to = "--path-vmc=\"/data/PS2/saves/SCUS-97353\"";
    const auto at = value.find(from);
    if (at != std::string::npos) value.replace(at, from.size(), to);
#endif
    return value;
}
profile_probe::Profile profile() {
    constexpr std::string_view manifest =
        "schema=1\nserial=SCUS-97353\nname=RAC3\nstatus=console-tested\n"
        "backend=wotm-v1\nlauncher=TEST12345\n"
        "config=/data/PS2/library/profiles/rac3-library.txt\n"
        "lua=/data/PS2/configs/rac3.lua\nsize=4379377664\n";
    profile_probe::Profile value{};
    CHECK(profile_probe::parse_profile(manifest, "SCUS-97353", value) == profile_probe::Parse::ok);
    return value;
}
void reset() {
    calls = {}; files.clear(); open_files.clear(); active.clear(); next_fd = 20; fake_error = 0;
    max_read = max_write = 1 << 20; fail_write = fail_sync = fail_close = fail_rename = fail_unlink = -1;
    oversize_read = -1; master_opens = 0; change_master_on_open = -1;
    files["/data/PS2/library/profiles/rac3-library.txt"] = config();
    files[apply_probe::master_path] = "--old-master=1\n";
}
int io_total() { int total = 0; for (int value : calls) total += value; return total; }
void run(apply_probe::Transaction& transaction) {
    unsigned frames = 0;
    while (transaction.busy() && frames++ < 20000) {
        const int before = io_total(); transaction.step(); CHECK(io_total() - before <= 1);
    }
    CHECK(!transaction.busy());
}
}

namespace apply_probe {
int open_read_only(const char* path) noexcept {
    ++calls[0]; const auto found = files.find(path);
    if (found == files.end()) { fake_error = ENOENT; return -1; }
    const int fd = next_fd++;
    open_files[fd] = {&found->second, 0}; active = path;
    if (active == master_path && ++master_opens == change_master_on_open) files[active] = "--changed-externally=1\n";
    return fd;
}
int create_exclusive(const char* path) noexcept {
    ++calls[1]; if (files.contains(path)) { fake_error = EEXIST; return -1; }
    files[path] = {}; const int fd = next_fd++;
    open_files[fd] = {&files[path], 0}; active = path; return fd;
}
int read_data(int fd, void* target, std::size_t size) noexcept {
    const auto handle = open_files.find(fd); CHECK(handle != open_files.end());
    const int call = calls[2]++;
    if (call == oversize_read) return static_cast<int>(size + 1);
    auto& file = *handle->second.data; auto& position = handle->second.pos;
    if (position >= file.size()) return 0;
    const auto amount = std::min({size, static_cast<std::size_t>(max_read), file.size() - position});
    std::copy_n(file.begin() + static_cast<std::ptrdiff_t>(position), amount, static_cast<char*>(target));
    position += amount; return static_cast<int>(amount);
}
int write_data(int fd, const void* source, std::size_t size) noexcept {
    const auto handle = open_files.find(fd); CHECK(handle != open_files.end());
    const int call = calls[3]++;
    if (call == fail_write) { fake_error = 5; return -1; }
    const auto amount = std::min(size, static_cast<std::size_t>(max_write));
    auto& file = *handle->second.data; auto& position = handle->second.pos;
    if (position + amount > file.size()) file.resize(position + amount);
    std::copy_n(static_cast<const char*>(source), amount, file.begin() + static_cast<std::ptrdiff_t>(position));
    position += amount; return static_cast<int>(amount);
}
int sync_data(int fd) noexcept {
    CHECK(open_files.contains(fd)); const int call = calls[4]++;
    if (call == fail_sync) { fake_error = 5; return -1; } return 0;
}
int close_data(int fd) noexcept {
    CHECK(open_files.contains(fd)); const int call = calls[5]++; open_files.erase(fd);
    if (call == fail_close) { fake_error = 9; return -1; } return 0;
}
int replace_file(const char* source, const char* target) noexcept {
    const int call = calls[6]++; if (call == fail_rename) { fake_error = 5; return -1; }
    const auto found = files.find(source); if (found == files.end()) { fake_error = 2; return -1; }
    files[target] = found->second; files.erase(found); return 0;
}
int remove_file(const char* path) noexcept {
    const int call = calls[7]++; if (call == fail_unlink) { fake_error = 5; return -1; }
    const auto erased = files.erase(path); if (!erased) { fake_error = 2; return -1; } return 0;
}
int last_error() noexcept { return fake_error; }
} // namespace apply_probe

int main() {
    using namespace apply_probe;
    const auto tested = profile();
    const auto selected = applied_config();
    const std::string original = "--old-master=1\n";
    // Static: the transaction spans ~220 KiB with card buffers; keep the
    // frame small for constrained host stacks. Product keeps its global.
    static Transaction transaction;
    transaction = {};
    CHECK(reinterpret_cast<std::uintptr_t>(&transaction) % buffer_alignment == 0);

    reset(); CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.state == State::done && transaction.failure == Failure::none);
    CHECK(transaction.master_replaced && transaction.backup_verified && transaction.backup_index == 1);
    CHECK(files[master_path] == selected && files[transaction.backup_path.data()] == original);
    CHECK(!files.contains(lock_path) && !files.contains(transaction.stage_path.data()));
    CHECK(transaction.summary() == "MASTER APPLIED VERIFIED AND BACKED UP");
    const auto mutation_calls = calls;
    CHECK(transaction.request_active_check()); run(transaction);
    CHECK(transaction.state == State::active_ready && transaction.summary() == "ACTIVE MASTER RECHECK PASSED - LAUNCH READY");
    CHECK(calls[3] == mutation_calls[3] && calls[4] == mutation_calls[4] && calls[6] == mutation_calls[6] && calls[7] == mutation_calls[7]);

    // UI display selections replace the corresponding backend defaults in the
    // staged master, while preserving every unrelated CLI line.
    reset(); transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso",
                              option_widescreen | option_upscaling)); run(transaction);
    const auto& applied_options = files[master_path];
    CHECK(applied_options.find("--host-display-mode=16:9\n") != std::string::npos);
    CHECK(applied_options.find("--gs-uprender=2x2\n") != std::string::npos);
    CHECK(applied_options.find("--gs-upscale=edgesmooth\n") != std::string::npos);
    CHECK(applied_options.find("--host-display-mode=4:3") == std::string::npos);
    CHECK(applied_options.find("--gs-uprender=none") == std::string::npos);
    CHECK(applied_options.find("--gs-upscale=none") == std::string::npos);
#if defined(PS2_LIBRARY_SHARED_VMC) && PS2_LIBRARY_SHARED_VMC
    CHECK(applied_options.find("--path-vmc=\"/data/PS2/saves/SCUS-97353\"\n") != std::string::npos);
#else
    CHECK(applied_options.find("--path-vmc=\"/tmp/vmc\"\n") != std::string::npos);
#endif
    CHECK(applied_options.find("--ee-hook=1,A,,2\n") != std::string::npos);

    // A matching master can be verified on a fresh app run without any mutation or backup.
    reset(); files[master_path] = selected; transaction = {};
    CHECK(transaction.request_existing_check(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.state == State::active_ready && !transaction.master_replaced);
    CHECK(calls[1] == 0 && calls[3] == 0 && calls[4] == 0 && calls[6] == 0 && calls[7] == 0);
    CHECK(transaction.summary() == "ACTIVE MASTER RECHECK PASSED - LAUNCH READY");

    // A mismatch is reported read-only, leaving the active master untouched.
    reset(); transaction = {};
    CHECK(transaction.request_existing_check(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::active_mismatch && files[master_path] == original);
    CHECK(calls[1] == 0 && calls[3] == 0 && calls[4] == 0 && calls[6] == 0 && calls[7] == 0);

    // Existing backup is never overwritten; the next unique name is retained.
    reset(); files[std::string(master_path) + ".library-backup-0001"] = "KEEP"; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.state == State::done && transaction.backup_index == 2);
    CHECK(files[std::string(master_path) + ".library-backup-0001"] == "KEEP");
    CHECK(files[std::string(master_path) + ".library-backup-0002"] == original);

    // Lock refusal performs no writes or replacement.
    reset(); files[lock_path] = "foreign"; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::lock_create && files[master_path] == original);
    CHECK(files[lock_path] == "foreign" && calls[3] == 0 && calls[6] == 0 && calls[7] == 0);

    // Invalid selected CLI is rejected before lock or backup creation.
    reset(); files["/data/PS2/library/profiles/rac3-library.txt"] += "--config=evil\n"; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::config_invalid && files[master_path] == original);
    CHECK(calls[1] == 0 && calls[3] == 0 && calls[6] == 0);

    // Partial writes are supported and still produce exact verified files.
    reset(); max_write = 7; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.state == State::done && files[master_path] == selected && calls[3] > 2);

    // Backup failure removes only the incomplete owned backup and lock.
    reset(); fail_write = 0; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::backup_write && files[master_path] == original);
    CHECK(!files.contains(lock_path) && !files.contains(std::string(master_path) + ".library-backup-0001"));

    // Stage failure preserves the already-verified backup and original master.
    reset(); fail_write = 1; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::stage_write && files[master_path] == original);
    CHECK(files[std::string(master_path) + ".library-backup-0001"] == original);
    CHECK(!files.contains(lock_path) && !files.contains(std::string(master_path) + ".library-backup-0001.pending"));

    // Concurrent master change cancels replacement and preserves both values.
    reset(); change_master_on_open = 2; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::master_changed && files[master_path] == "--changed-externally=1\n");
    CHECK(files[std::string(master_path) + ".library-backup-0001"] == original && calls[6] == 0);

    // Rename failure leaves original active, preserves backup, and removes owned stage/lock.
    reset(); fail_rename = 0; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::rename && files[master_path] == original);
    CHECK(files[std::string(master_path) + ".library-backup-0001"] == original);
    CHECK(!files.contains(lock_path) && !files.contains(std::string(master_path) + ".library-backup-0001.pending"));

    // Active readback failure records that replacement happened and locks out retries.
    reset(); change_master_on_open = 3; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::active_verify && transaction.master_replaced);
    CHECK(!transaction.clear() && !transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso"));
    CHECK(files[std::string(master_path) + ".library-backup-0001"] == original);

    // A change after a successful transaction is caught by the final launch-time recheck.
    reset(); transaction = {}; CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    files[master_path] = "--changed-after-apply=1\n"; CHECK(transaction.request_active_check()); run(transaction);
    CHECK(transaction.failure == Failure::final_changed && transaction.master_replaced && !transaction.clear());

    // Close ambiguity requires app restart and never reaches mutation when it occurs on config close.
    reset(); fail_close = 0; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::close && files[master_path] == original && !transaction.clear());

    // Bounded reads reject oversized returns and operation exhaustion before mutation.
    reset(); oversize_read = 0; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::config_read && calls[1] == 0);
    reset(); max_read = 1; files["/data/PS2/library/profiles/rac3-library.txt"] = std::string(13000, '#') + "\n" + config(); transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::operation_limit && calls[1] == 0 && files[master_path] == original);

    // Loader path: supplied master text is compared and replaced verbatim.
    const std::string loader =
        "# PS2 Library loader - lotr. Managed on every launch; edit the game files instead.\n"
        "--config=\"/data/PS2/configs/lotr.txt\"\n"
        "--config-local-lua=\"/data/PS2/configs/lotr.lua\"\n";
    reset(); files[master_path] = loader; transaction = {};
    CHECK(transaction.request_loader_check(loader.c_str(), loader.size(), "SLES-52017"));
    run(transaction);
    CHECK(transaction.state == State::active_ready && !transaction.master_replaced);
    CHECK(calls[1] == 0 && calls[3] == 0 && calls[4] == 0 && calls[6] == 0 && calls[7] == 0);
    reset(); transaction = {};
    CHECK(transaction.request_loader_replace(loader.c_str(), loader.size(), "SLES-52017"));
    run(transaction);
    CHECK(transaction.state == State::done && transaction.master_replaced);
    CHECK(files[master_path] == loader && files[std::string(master_path) + ".library-backup-0001"] == original);
    reset(); transaction = {};
    CHECK(!transaction.request_loader_replace(nullptr, 0, "SLES-52017"));
    CHECK(transaction.failure == Failure::invalid_request && calls[0] == 0 && calls[1] == 0);
#if defined(PS2_LIBRARY_SHARED_VMC) && PS2_LIBRARY_SHARED_VMC
    // Loader replace also backs up the serial-addressed shared card first.
    reset();
    files["/data/PS2/saves/SLES-52017/VMC0.card"] = "CARD-BYTES";
    transaction = {};
    CHECK(transaction.request_loader_replace(loader.c_str(), loader.size(), "SLES-52017"));
    run(transaction);
    CHECK(transaction.state == State::done && transaction.card_backups_ == 1);
    CHECK(files["/data/PS2/saves/SLES-52017/VMC0.card.backup-0001"] == "CARD-BYTES");
    CHECK(files[master_path] == loader);
#endif

    reset();
    profile_probe::Profile untested = tested; untested.status = profile_probe::Status::review_required;
    static Transaction invalid; invalid = {};
    CHECK(!invalid.request(untested, "/data/PS2/isos/Ratchet & Clank 3.iso"));
    CHECK(invalid.failure == Failure::invalid_request && calls[1] == 0);

#if defined(PS2_LIBRARY_SHARED_VMC) && PS2_LIBRARY_SHARED_VMC
    const std::string card0 = "/data/PS2/saves/SCUS-97353/VMC0.card";
    const std::string card1 = "/data/PS2/saves/SCUS-97353/VMC1.card";
    auto pattern = [](std::size_t size) {
        std::string value; value.reserve(size);
        for (std::size_t i = 0; i < size; ++i) value.push_back(static_cast<char>((i * 31 + 7) & 0xFF));
        return value;
    };
    // Static seed: a 16 MiB automatic here blows past constrained host stacks.
    auto oversize_seed = []() -> const std::string& {
        static std::string seed;
        if (seed.empty()) seed.assign(apply_probe::max_card_bytes + 1024, 'x');
        return seed;
    };
    // Existing shared card is copied byte-for-byte before the master moves.
    reset(); files[card0] = pattern(20000); transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.state == State::done && transaction.failure == Failure::none);
    CHECK(transaction.card_backups_ == 1 && files[master_path] == selected);
    CHECK(files[card0 + ".backup-0001"] == files[card0] && files[card0].size() == 20000);
    CHECK(transaction.summary() == "MASTER APPLIED - VMC CARD BACKED UP");
    CHECK(!files.contains(lock_path));

    // Partial host reads/writes still produce an exact multi-chunk copy.
    reset(); files[card0] = pattern(70000); max_read = 1000; max_write = 7000; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.state == State::done && files[card0 + ".backup-0001"] == pattern(70000));

    // Both slots are backed up; taken names are never overwritten.
    reset(); files[card0] = pattern(5000); files[card1] = pattern(9000);
    files[card0 + ".backup-0001"] = "KEEP"; transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.state == State::done && transaction.card_backups_ == 2);
    CHECK(files[card0 + ".backup-0001"] == "KEEP" && files[card0 + ".backup-0002"] == pattern(5000));
    CHECK(files[card1 + ".backup-0001"] == pattern(9000));

    // VMC1 alone is backed up while absent VMC0 is skipped silently.
    reset(); files[card1] = pattern(100); transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.state == State::done && transaction.card_backups_ == 1);
    CHECK(files[card1 + ".backup-0001"] == pattern(100));

    // First run with no cards keeps the original done message and writes no card files.
    reset(); transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.state == State::done && transaction.card_backups_ == 0);
    CHECK(transaction.summary() == "MASTER APPLIED VERIFIED AND BACKED UP");
    for (const auto& entry : files) CHECK(entry.first.find("/data/PS2/saves/") != 0);

    // Oversized cards abort before any master write; partial copies are removed and retry is allowed.
    // NOTE(bisect): 16 MiB seed under investigation for host-only stack bloat.
    reset(); files[card0] = oversize_seed(); transaction = {};
    CHECK(transaction.request(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.failure == Failure::card_too_large && files[master_path] == original);
    CHECK(!files.contains(card0 + ".backup-0001") && !files.contains(lock_path));
    CHECK(transaction.clear());

    // Read-only checks never back up cards even when they exist.
    reset(); files[card0] = pattern(100); files[master_path] = selected; transaction = {};
    CHECK(transaction.request_existing_check(tested, "/data/PS2/isos/Ratchet & Clank 3.iso")); run(transaction);
    CHECK(transaction.state == State::active_ready && transaction.card_backups_ == 0);
    CHECK(calls[1] == 0 && calls[3] == 0 && calls[4] == 0 && calls[6] == 0 && calls[7] == 0);
    for (const auto& entry : files) CHECK(entry.first.find(".backup-") == std::string::npos);
#endif
    std::printf("Passed %u host checks: transactional backup, atomic activation, cleanup, and final recheck.\n", checks);
}
