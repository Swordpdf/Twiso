// Transactional master activation for PS2 Library. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "profile_check.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace apply_probe {
inline constexpr char master_path[] = "/data/PS2/configs/master.txt";
inline constexpr char lock_path[] = "/data/PS2/configs/.ps2-library.lock";
inline constexpr std::size_t max_text_size = profile_probe::max_text_size;
inline constexpr std::size_t io_chunk = 4096;
inline constexpr std::size_t max_backup_index = 9999;
inline constexpr std::size_t max_operations = 12000;
inline constexpr std::size_t buffer_alignment = 0x4000;
// Shared-VMC card backup: raw PS2 cards are small (8 MiB); the cap only
// guards against unbounded copies. Flat suffixed names avoid mkdir/stat.
inline constexpr std::size_t max_card_bytes = 16U * 1024U * 1024U;
inline constexpr std::size_t card_chunk_bytes = 16U * 1024U;
inline constexpr std::size_t max_card_backup_index = 9999;
inline constexpr std::size_t card_slots = 2;

// UI selections that can be represented directly in the emulator CLI.  The
// remaining checkboxes are intentionally kept out of this mask until their
// exact backend syntax is verified.
enum OptionBits : std::uint32_t {
    option_widescreen = 1U << 0,
    option_upscaling = 1U << 1,
};
inline constexpr std::uint32_t supported_option_bits = option_widescreen | option_upscaling;

int open_read_only(const char* path) noexcept;
int create_exclusive(const char* path) noexcept;
int read_data(int descriptor, void* buffer, std::size_t size) noexcept;
int write_data(int descriptor, const void* buffer, std::size_t size) noexcept;
int sync_data(int descriptor) noexcept;
int close_data(int descriptor) noexcept;
int replace_file(const char* source, const char* target) noexcept;
int remove_file(const char* path) noexcept;
int last_error() noexcept;

enum class State {
    idle,
    opening_config, reading_config, closing, validating_config,
    opening_master, reading_master, validating_master,
    card_open_src, card_name_dst, card_read, card_write, card_sync,
    card_close_dst, card_close_src,
    card_abort_close_dst, card_abort_close_src, card_abort_unlink,
    creating_lock, choosing_backup, writing_backup, syncing_backup,
    opening_backup_verify, reading_backup_verify, validating_backup,
    creating_stage, writing_stage, syncing_stage,
    opening_stage_verify, reading_stage_verify, validating_stage,
    opening_master_recheck, reading_master_recheck, validating_master_recheck,
    renaming_stage, opening_active_verify, reading_active_verify, validating_active,
    removing_stage, removing_backup, removing_lock,
    done,
    opening_final_check, reading_final_check, validating_final_check,
    active_ready,
    failed
};
enum class Failure {
    none, invalid_request, operation_limit,
    config_open, config_read, config_too_large, config_invalid,
    master_open, master_read, master_too_large, master_invalid,
    card_open, card_create, card_read, card_write, card_sync,
    card_close, card_limit, card_too_large,
    lock_create, backup_limit, backup_create, backup_write, backup_sync,
    backup_read, backup_too_large, backup_verify,
    stage_create, stage_write, stage_sync, stage_read, stage_too_large, stage_verify,
    master_changed, rename, active_open, active_read, active_too_large, active_verify,
    close, cleanup, final_open, final_read, final_too_large, final_changed,
    active_mismatch
};

class Transaction final {
public:
    State state = State::idle;
    Failure failure = Failure::none;
    std::size_t operations = 0, selected_size = 0, original_size = 0, verify_size = 0;
    std::size_t backup_index = 0;
    std::size_t card_slot_ = 0, card_backup_index_ = 0, card_total_ = 0, card_have_ = 0, card_sent_ = 0, card_backups_ = 0;
    int open_result = 0, read_result = 0, write_result = 0, sync_result = 0;
    int close_result = 0, rename_result = 0, unlink_result = 0, error = 0;
    bool master_replaced = false, backup_verified = false;
    std::array<char, 65> active_path{};
    std::array<char, 385> backup_path{};
    std::array<char, 401> stage_path{};
    int card_src_ = -1, card_dst_ = -1;
    Failure card_abort_ = Failure::none;
    bool card_dst_created_ = false;
    std::array<char, 96> card_src_path_{};
    std::array<char, 112> card_dst_path_{};
    std::array<char, card_chunk_bytes> card_chunk_{};

    bool busy() const noexcept;
    bool clear() noexcept;
    bool request(const profile_probe::Profile& profile, std::string_view iso_path,
                 std::uint32_t option_mask = 0) noexcept;
    bool request_existing_check(const profile_probe::Profile& profile, std::string_view iso_path,
                                std::uint32_t option_mask = 0) noexcept;
    // Per-game loader path: selected_ is supplied verbatim (already final),
    // so validation starts at the current master. Serial feeds card backup.
    bool request_loader_check(const char* loader, std::size_t loader_size, const char* serial) noexcept;
    bool request_loader_replace(const char* loader, std::size_t loader_size, const char* serial) noexcept;
    bool request_active_check() noexcept;
    void step() noexcept; // At most one platform I/O operation per call.
    std::string_view summary() const noexcept;

private:
    enum class Buffer { selected, original, verify };
    int descriptor_ = -1;
    Buffer target_ = Buffer::verify;
    State after_close_ = State::idle;
    State finish_after_cleanup_ = State::failed;
    Failure pending_failure_ = Failure::none;
    std::size_t limit_ = 0, written_ = 0;
    bool lock_owned_ = false, backup_created_ = false, stage_created_ = false;
    bool verify_only_ = false;
    std::uint32_t option_mask_ = 0;
    profile_probe::Profile profile_{};
    std::array<char, 272> iso_path_{};
    alignas(buffer_alignment) std::array<char, max_text_size + 1> selected_{};
    alignas(buffer_alignment) std::array<char, max_text_size + 1> original_{};
    alignas(buffer_alignment) std::array<char, max_text_size + 1> verify_{};

    bool operation() noexcept;
    void begin_failure(Failure reason) noexcept;
    void begin_cleanup(State finish) noexcept;
    void advance_cleanup() noexcept;
    void begin_read(const char* path, Buffer target, State reading, Failure failure,
                    std::size_t limit) noexcept;
    void take_read(State next, Failure read_failure, Failure size_failure) noexcept;
    void close_current() noexcept;
    void take_write(const char* source, std::size_t size, State next, Failure failure) noexcept;
    void take_sync(State next, Failure failure) noexcept;
    bool build_backup_path() noexcept;
    bool build_stage_path() noexcept;
    bool request_loader_common(const char* loader, std::size_t loader_size,
                               const char* serial, bool write) noexcept;
    bool build_card_src_path() noexcept;
    bool build_card_dst_path() noexcept;
};
static_assert(alignof(Transaction) == buffer_alignment);
} // namespace apply_probe
