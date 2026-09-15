// SPDX-License-Identifier: GPL-3.0-or-later
#include "apply_store.hpp"
#include <cerrno>

namespace apply_probe {
namespace {
bool copy(std::string_view value, char* target, std::size_t capacity) noexcept {
    if (value.empty() || value.size() >= capacity) return false;
    for (std::size_t i = 0; i < value.size(); ++i) target[i] = value[i];
    target[value.size()] = 0; return true;
}
bool same(const char* a, std::size_t a_size, const char* b, std::size_t b_size) noexcept {
    if (a_size != b_size) return false;
    for (std::size_t i = 0; i < a_size; ++i) if (a[i] != b[i]) return false;
    return true;
}
bool append(char* target, std::size_t capacity, std::size_t& at, std::string_view value) noexcept {
    if (at + value.size() + 1 > capacity) return false;
    for (char c : value) target[at++] = c;
    target[at] = 0; return true;
}
bool option_line(std::string_view line, std::string_view key) noexcept {
    return line.size() > key.size() && line.substr(0, key.size()) == key && line[key.size()] == '=';
}
bool rewrite_ui_options(char* text, std::size_t capacity, std::size_t& size,
                        std::uint32_t mask, char* scratch) noexcept {
    const auto supported = mask & supported_option_bits;
    if (!supported) return true;
    std::size_t at = 0, out = 0;
    while (at < size) {
        std::size_t line_end = at;
        while (line_end < size && text[line_end] != '\n') ++line_end;
        if (line_end < size) ++line_end;
        std::size_t content_end = line_end;
        if (content_end > at && text[content_end - 1] == '\n') --content_end;
        if (content_end > at && text[content_end - 1] == '\r') --content_end;
        const std::string_view line{text + at, content_end - at};
        const bool skip_wide = (supported & option_widescreen) && option_line(line, "--host-display-mode");
        const bool skip_uprender = (supported & option_upscaling) && option_line(line, "--gs-uprender");
        const bool skip_upscale = (supported & option_upscaling) && option_line(line, "--gs-upscale");
        if (!skip_wide && !skip_uprender && !skip_upscale &&
            !append(scratch, capacity, out, {text + at, line_end - at})) return false;
        at = line_end;
    }
    if (out && scratch[out - 1] != '\n' && !append(scratch, capacity, out, "\n")) return false;
    if ((supported & option_widescreen) && !append(scratch, capacity, out, "--host-display-mode=16:9\n")) return false;
    if (supported & option_upscaling) {
        if (!append(scratch, capacity, out, "--gs-uprender=2x2\n")) return false;
        if (!append(scratch, capacity, out, "--gs-upscale=edgesmooth\n")) return false;
    }
    for (std::size_t i = 0; i < out; ++i) text[i] = scratch[i];
    size = out; text[size] = 0;
    return true;
}
}

bool Transaction::busy() const noexcept {
    return state != State::idle && state != State::done && state != State::active_ready && state != State::failed;
}
bool Transaction::clear() noexcept {
    if (busy() || descriptor_ >= 0 || card_src_ >= 0 || card_dst_ >= 0 || lock_owned_ || stage_created_ ||
        (state == State::failed && (master_replaced || failure == Failure::close ||
                                    failure == Failure::cleanup))) return false;
    state = State::idle; failure = Failure::none; operations = selected_size = original_size = verify_size = 0;
    backup_index = 0; card_slot_ = card_backup_index_ = card_total_ = card_have_ = card_sent_ = card_backups_ = 0;
    open_result = read_result = write_result = sync_result = 0;
    close_result = rename_result = unlink_result = error = 0;
    card_src_ = card_dst_ = -1; card_abort_ = Failure::none; card_dst_created_ = false;
    card_src_path_.fill(0); card_dst_path_.fill(0); card_chunk_.fill(0);
    master_replaced = backup_verified = false; active_path.fill(0); backup_path.fill(0); stage_path.fill(0);
    descriptor_ = -1; target_ = Buffer::verify; after_close_ = State::idle;
    finish_after_cleanup_ = State::failed; pending_failure_ = Failure::none;
    limit_ = written_ = 0; lock_owned_ = backup_created_ = stage_created_ = false; verify_only_ = false;
    option_mask_ = 0;
    profile_ = {}; iso_path_.fill(0); return true;
}
bool Transaction::request(const profile_probe::Profile& profile, std::string_view iso_path,
                          std::uint32_t option_mask) noexcept {
    if (!clear()) return false;
    if ((profile.status != profile_probe::Status::console_tested && profile.status != profile_probe::Status::as_is) || profile.serial[0] == 0 ||
        profile.config[0] == 0 || profile.launcher[0] == 0 || iso_path.empty() ||
        !copy(iso_path, iso_path_.data(), iso_path_.size())) {
        state = State::failed; failure = Failure::invalid_request; return false;
    }
    verify_only_ = false; option_mask_ = option_mask & supported_option_bits;
    profile_ = profile; copy("CONFIG", active_path.data(), active_path.size());
    state = State::opening_config; return true;
}
bool Transaction::request_existing_check(const profile_probe::Profile& profile, std::string_view iso_path,
                                         std::uint32_t option_mask) noexcept {
    if (!clear()) return false;
    if ((profile.status != profile_probe::Status::console_tested && profile.status != profile_probe::Status::as_is) || profile.serial[0] == 0 ||
        profile.config[0] == 0 || profile.launcher[0] == 0 || iso_path.empty() ||
        !copy(iso_path, iso_path_.data(), iso_path_.size())) {
        state = State::failed; failure = Failure::invalid_request; return false;
    }
    verify_only_ = true; option_mask_ = option_mask & supported_option_bits;
    profile_ = profile; copy("READ-ONLY CONFIG", active_path.data(), active_path.size());
    state = State::opening_config; return true;
}
bool Transaction::request_loader_common(const char* loader, std::size_t loader_size,
                                          const char* serial, bool write) noexcept {
    if (!clear()) return false;
    if (loader == nullptr || loader_size == 0 || loader_size > max_text_size ||
        serial == nullptr || serial[0] == 0) {
        state = State::failed; failure = Failure::invalid_request; return false;
    }
    std::size_t serial_length = 0;
    while (serial[serial_length] != 0) {
        if (serial_length >= profile_.serial.size() - 1) {
            state = State::failed; failure = Failure::invalid_request; return false;
        }
        ++serial_length;
    }
    for (std::size_t i = 0; i < loader_size; ++i) selected_[i] = loader[i];
    selected_[loader_size] = 0; selected_size = loader_size;
    for (std::size_t i = 0; i <= serial_length; ++i) profile_.serial[i] = serial[i];
    verify_only_ = !write; option_mask_ = 0;
    copy("GAME LOADER", active_path.data(), active_path.size());
    state = State::opening_master; return true;
}
bool Transaction::request_loader_check(const char* loader, std::size_t loader_size, const char* serial) noexcept {
    return request_loader_common(loader, loader_size, serial, false);
}
bool Transaction::request_loader_replace(const char* loader, std::size_t loader_size, const char* serial) noexcept {
    return request_loader_common(loader, loader_size, serial, true);
}
bool Transaction::request_active_check() noexcept {
    if ((state != State::done && state != State::active_ready) || !master_replaced || !selected_size) return false;
    verify_size = 0; error = 0; copy("FINAL MASTER", active_path.data(), active_path.size());
    state = State::opening_final_check; return true;
}
bool Transaction::operation() noexcept {
    if (operations >= max_operations) { begin_failure(Failure::operation_limit); return false; }
    ++operations; return true;
}
void Transaction::begin_failure(Failure reason) noexcept {
    if (failure == Failure::none) failure = reason;
    if (descriptor_ >= 0) {
        pending_failure_ = failure; after_close_ = State::failed; state = State::closing;
    } else begin_cleanup(State::failed);
}
void Transaction::begin_cleanup(State finish) noexcept {
    finish_after_cleanup_ = finish;
    if (stage_created_) state = State::removing_stage;
    else if (backup_created_ && !backup_verified) state = State::removing_backup;
    else if (lock_owned_) state = State::removing_lock;
    else state = finish;
}
void Transaction::advance_cleanup() noexcept {
    if (stage_created_) state = State::removing_stage;
    else if (backup_created_ && !backup_verified) state = State::removing_backup;
    else if (lock_owned_) state = State::removing_lock;
    else state = finish_after_cleanup_;
}
void Transaction::begin_read(const char* path, Buffer target, State reading, Failure reason,
                             std::size_t limit) noexcept {
    if (!operation()) return;
    open_result = open_read_only(path);
    if (open_result < 0) { error = last_error(); begin_failure(reason); return; }
    descriptor_ = open_result; target_ = target; limit_ = limit;
    if (target == Buffer::selected) selected_size = 0;
    else if (target == Buffer::original) original_size = 0;
    else verify_size = 0;
    state = reading;
}
void Transaction::take_read(State next, Failure read_failure, Failure size_failure) noexcept {
    std::size_t* size = target_ == Buffer::selected ? &selected_size :
        (target_ == Buffer::original ? &original_size : &verify_size);
    char* buffer = target_ == Buffer::selected ? selected_.data() :
        (target_ == Buffer::original ? original_.data() : verify_.data());
    if (!operation()) return;
    const auto remaining = limit_ + 1 - *size;
    const auto amount = remaining < io_chunk ? remaining : io_chunk;
    read_result = read_data(descriptor_, buffer + *size, amount);
    if (read_result < 0) { error = last_error(); begin_failure(read_failure); return; }
    if (static_cast<std::size_t>(read_result) > amount) { begin_failure(read_failure); return; }
    if (read_result == 0) { after_close_ = next; state = State::closing; return; }
    *size += static_cast<std::size_t>(read_result);
    if (*size > limit_) begin_failure(size_failure);
}
void Transaction::close_current() noexcept {
    // Close and cleanup are mandatory even when the non-cleanup cap was reached.
    ++operations; close_result = close_data(descriptor_); descriptor_ = -1;
    if (close_result != 0) {
        error = last_error(); if (failure == Failure::none) failure = Failure::close;
        pending_failure_ = failure;
    }
    if (pending_failure_ != Failure::none) {
        failure = pending_failure_; pending_failure_ = Failure::none; begin_cleanup(State::failed);
    } else state = after_close_;
}
void Transaction::take_write(const char* source, std::size_t size, State next, Failure reason) noexcept {
    if (written_ == size) { state = next; return; }
    if (!operation()) return;
    const auto left = size - written_; const auto amount = left < io_chunk ? left : io_chunk;
    write_result = write_data(descriptor_, source + written_, amount);
    if (write_result <= 0 || static_cast<std::size_t>(write_result) > amount) {
        if (write_result < 0) error = last_error();
        begin_failure(reason); return;
    }
    written_ += static_cast<std::size_t>(write_result);
    if (written_ == size) state = next;
}
void Transaction::take_sync(State next, Failure reason) noexcept {
    if (!operation()) return;
    sync_result = sync_data(descriptor_);
    if (sync_result != 0) { error = last_error(); begin_failure(reason); return; }
    after_close_ = next; state = State::closing;
}
bool Transaction::build_backup_path() noexcept {
    backup_path.fill(0); std::size_t at = 0;
    if (!append(backup_path.data(), backup_path.size(), at, master_path) ||
        !append(backup_path.data(), backup_path.size(), at, ".library-backup-")) return false;
    const unsigned value = static_cast<unsigned>(backup_index);
    const std::array<char, 4> digits{
        static_cast<char>('0' + value / 1000 % 10), static_cast<char>('0' + value / 100 % 10),
        static_cast<char>('0' + value / 10 % 10), static_cast<char>('0' + value % 10)};
    return append(backup_path.data(), backup_path.size(), at, {digits.data(), digits.size()});
}
bool Transaction::build_stage_path() noexcept {
    stage_path.fill(0); std::size_t at = 0;
    return append(stage_path.data(), stage_path.size(), at, backup_path.data()) &&
        append(stage_path.data(), stage_path.size(), at, ".pending");
}
bool Transaction::build_card_src_path() noexcept {
    // "/data/PS2/saves/" + serial + "/VMC<slot>.card", mirroring the
    // shared-VMC rewrite target. Serial bytes were validated with the CLI.
    card_src_path_.fill(0); std::size_t at = 0;
    if (!append(card_src_path_.data(), card_src_path_.size(), at, "/data/PS2/saves/")) return false;
    std::size_t serial_length = 0;
    while (serial_length < profile_.serial.size() && profile_.serial[serial_length] != 0) ++serial_length;
    if (serial_length == 0 || serial_length > 10) return false;
    for (std::size_t i = 0; i < serial_length; ++i) {
        const char c = profile_.serial[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
        if (at + 2 > card_src_path_.size()) return false;
        card_src_path_[at++] = c; card_src_path_[at] = 0;
    }
    if (!append(card_src_path_.data(), card_src_path_.size(), at, "/VMC")) return false;
    if (card_slot_ >= card_slots || at + 7 > card_src_path_.size()) return false;
    card_src_path_[at++] = static_cast<char>('0' + card_slot_); card_src_path_[at] = 0;
    return append(card_src_path_.data(), card_src_path_.size(), at, ".card");
}
bool Transaction::build_card_dst_path() noexcept {
    card_dst_path_.fill(0); std::size_t at = 0;
    std::string_view src{card_src_path_.data()};
    if (src.empty()) return false;
    { std::size_t n = 0; while (n < card_src_path_.size() && card_src_path_[n] != 0) ++n; src = std::string_view{card_src_path_.data(), n}; }
    if (!append(card_dst_path_.data(), card_dst_path_.size(), at, src) ||
        !append(card_dst_path_.data(), card_dst_path_.size(), at, ".backup-")) return false;
    const unsigned value = static_cast<unsigned>(card_backup_index_);
    const std::array<char, 4> digits{
        static_cast<char>('0' + value / 1000 % 10), static_cast<char>('0' + value / 100 % 10),
        static_cast<char>('0' + value / 10 % 10), static_cast<char>('0' + value % 10)};
    return append(card_dst_path_.data(), card_dst_path_.size(), at, {digits.data(), digits.size()});
}

void Transaction::step() noexcept {
    switch (state) {
    case State::opening_config:
        begin_read(profile_.config.data(), Buffer::selected, State::reading_config, Failure::config_open, max_text_size); return;
    case State::reading_config:
        take_read(State::validating_config, Failure::config_read, Failure::config_too_large); return;
    case State::closing: close_current(); return;
    case State::validating_config:
        selected_[selected_size] = 0;
        if (!profile_probe::validate_config({selected_.data(), selected_size}, profile_, iso_path_.data())) {
            begin_failure(Failure::config_invalid); return;
        }
        // The default build keeps the maker's native --path-vmc="/tmp/vmc"
        // setting.  The shared-VMC test build deliberately rewrites that
        // directory to the selected disc serial, allowing different backend
        // PKGs to use the same raw PS2 card without touching encrypted native
        // PS5 savedata containers.
        if (profile_.status == profile_probe::Status::as_is &&
            !profile_probe::rewrite_as_is_image(selected_.data(), selected_.size(), selected_size,
                                                iso_path_.data())) {
            begin_failure(Failure::config_invalid); return;
        }
#if defined(PS2_LIBRARY_SHARED_VMC) && PS2_LIBRARY_SHARED_VMC
        if (!profile_probe::rewrite_vmc_path(selected_.data(), selected_.size(), selected_size,
                                             profile_.serial.data())) {
            begin_failure(Failure::config_invalid); return;
        }
#endif
        if (!rewrite_ui_options(selected_.data(), selected_.size(), selected_size, option_mask_, verify_.data())) {
            begin_failure(Failure::config_invalid); return;
        }
        copy("CURRENT MASTER", active_path.data(), active_path.size()); state = State::opening_master; return;
    case State::opening_master:
        begin_read(master_path, Buffer::original, State::reading_master, Failure::master_open, max_text_size); return;
    case State::reading_master:
        take_read(State::validating_master, Failure::master_read, Failure::master_too_large); return;
    case State::validating_master:
        original_[original_size] = 0;
        if (!profile_probe::validate_master({original_.data(), original_size})) { begin_failure(Failure::master_invalid); return; }
        if (verify_only_) {
            if (!same(selected_.data(), selected_size, original_.data(), original_size)) {
                begin_failure(Failure::active_mismatch); return;
            }
            copy("EXISTING MASTER MATCH", active_path.data(), active_path.size()); state = State::active_ready; return;
        }
#if defined(PS2_LIBRARY_SHARED_VMC) && PS2_LIBRARY_SHARED_VMC
        // Timestamp-free card backup (Garlic-style safety for raw VMC):
        // copy each existing shared slot to a never-overwritten suffixed
        // file before the master is touched. Absent cards are skipped.
        card_slot_ = 0; card_backups_ = 0;
        copy("CARD BACKUP VMC0", active_path.data(), active_path.size()); state = State::card_open_src; return;
#else
        copy("WRITE LOCK", active_path.data(), active_path.size()); state = State::creating_lock; return;
#endif
    case State::card_open_src: {
        if (card_slot_ >= card_slots) {
            copy("WRITE LOCK", active_path.data(), active_path.size()); state = State::creating_lock; return;
        }
        if (!build_card_src_path()) { begin_failure(Failure::card_open); return; }
        if (!operation()) return;
        copy(card_slot_ == 0 ? "CARD BACKUP VMC0" : "CARD BACKUP VMC1", active_path.data(), active_path.size());
        open_result = open_read_only(card_src_path_.data());
        if (open_result < 0) {
            error = last_error();
            if (error == ENOENT) { ++card_slot_; return; }
            begin_failure(Failure::card_open); return;
        }
        card_src_ = open_result; card_backup_index_ = 0; card_total_ = 0; card_have_ = 0; card_sent_ = 0;
        card_dst_ = -1; card_dst_created_ = false; state = State::card_name_dst; return;
    }
    case State::card_name_dst:
        if (++card_backup_index_ > max_card_backup_index) {
            card_abort_ = Failure::card_limit; state = State::card_abort_close_src; return;
        }
        if (!build_card_dst_path()) { card_abort_ = Failure::card_create; state = State::card_abort_close_src; return; }
        if (!operation()) { card_abort_ = Failure::operation_limit; state = State::card_abort_close_src; return; }
        open_result = create_exclusive(card_dst_path_.data());
        if (open_result < 0) {
            error = last_error();
            if (error == EEXIST) return;
            card_abort_ = Failure::card_create; state = State::card_abort_close_src; return;
        }
        card_dst_ = open_result; card_dst_created_ = true; state = State::card_read; return;
    case State::card_read: {
        if (!operation()) { card_abort_ = Failure::operation_limit; state = State::card_abort_close_dst; return; }
        const std::size_t room = card_chunk_.size() - card_have_;
        read_result = read_data(card_src_, card_chunk_.data() + card_have_, room);
        if (read_result < 0) {
            error = last_error(); card_abort_ = Failure::card_read; state = State::card_abort_close_dst; return;
        }
        if (static_cast<std::size_t>(read_result) > room) {
            card_abort_ = Failure::card_read; state = State::card_abort_close_dst; return;
        }
        if (read_result == 0) { state = State::card_sync; return; }
        card_have_ += static_cast<std::size_t>(read_result);
        if (card_total_ + card_have_ > max_card_bytes) {
            card_abort_ = Failure::card_too_large; state = State::card_abort_close_dst; return;
        }
        state = State::card_write; return;
    }
    case State::card_write: {
        const std::size_t left = card_have_ - card_sent_;
        if (left == 0) { card_total_ += card_have_; card_have_ = card_sent_ = 0; state = State::card_read; return; }
        if (!operation()) { card_abort_ = Failure::operation_limit; state = State::card_abort_close_dst; return; }
        write_result = write_data(card_dst_, card_chunk_.data() + card_sent_, left);
        if (write_result <= 0 || static_cast<std::size_t>(write_result) > left) {
            if (write_result < 0) error = last_error();
            card_abort_ = Failure::card_write; state = State::card_abort_close_dst; return;
        }
        card_sent_ += static_cast<std::size_t>(write_result);
        if (card_sent_ == card_have_) { card_total_ += card_have_; card_have_ = card_sent_ = 0; state = State::card_read; }
        return;
    }
    case State::card_sync:
        if (!operation()) { card_abort_ = Failure::operation_limit; state = State::card_abort_close_dst; return; }
        sync_result = sync_data(card_dst_);
        if (sync_result != 0) {
            error = last_error(); card_abort_ = Failure::card_sync; state = State::card_abort_close_dst; return;
        }
        state = State::card_close_dst; return;
    case State::card_close_dst:
        ++operations; close_result = close_data(card_dst_); card_dst_ = -1;
        if (close_result != 0) {
            error = last_error(); card_abort_ = Failure::card_close; state = State::card_abort_close_src; return;
        }
        state = State::card_close_src; return;
    case State::card_close_src:
        ++operations; close_result = close_data(card_src_); card_src_ = -1;
        if (close_result != 0) {
            error = last_error(); card_abort_ = Failure::card_close; state = State::card_abort_unlink; return;
        }
        ++card_backups_; ++card_slot_; state = State::card_open_src; return;
    case State::card_abort_close_dst:
        if (card_dst_ >= 0) { ++operations; close_data(card_dst_); card_dst_ = -1; }
        state = State::card_abort_close_src; return;
    case State::card_abort_close_src:
        if (card_src_ >= 0) { ++operations; close_data(card_src_); card_src_ = -1; }
        state = State::card_abort_unlink; return;
    case State::card_abort_unlink:
        if (card_dst_created_) {
            ++operations; remove_file(card_dst_path_.data()); card_dst_created_ = false;
        }
        begin_failure(card_abort_ == Failure::none ? Failure::card_write : card_abort_); return;
    case State::creating_lock:
        if (!operation()) return;
        open_result = create_exclusive(lock_path);
        if (open_result < 0) { error = last_error(); begin_failure(Failure::lock_create); return; }
        descriptor_ = open_result; lock_owned_ = true; after_close_ = State::choosing_backup; state = State::closing; return;
    case State::choosing_backup:
        if (++backup_index > max_backup_index) { begin_failure(Failure::backup_limit); return; }
        if (!build_backup_path() || !operation()) { if (failure == Failure::none) begin_failure(Failure::backup_create); return; }
        open_result = create_exclusive(backup_path.data());
        if (open_result < 0) {
            error = last_error();
            if (error == EEXIST) return;
            begin_failure(Failure::backup_create); return;
        }
        descriptor_ = open_result; backup_created_ = true; written_ = 0;
        copy("BACKUP WRITE", active_path.data(), active_path.size()); state = State::writing_backup; return;
    case State::writing_backup:
        take_write(original_.data(), original_size, State::syncing_backup, Failure::backup_write); return;
    case State::syncing_backup:
        take_sync(State::opening_backup_verify, Failure::backup_sync); return;
    case State::opening_backup_verify:
        copy("BACKUP VERIFY", active_path.data(), active_path.size());
        begin_read(backup_path.data(), Buffer::verify, State::reading_backup_verify, Failure::backup_read, max_text_size); return;
    case State::reading_backup_verify:
        take_read(State::validating_backup, Failure::backup_read, Failure::backup_too_large); return;
    case State::validating_backup:
        if (!same(original_.data(), original_size, verify_.data(), verify_size)) { begin_failure(Failure::backup_verify); return; }
        backup_verified = true; if (!build_stage_path()) { begin_failure(Failure::stage_create); return; }
        copy("STAGE CREATE", active_path.data(), active_path.size()); state = State::creating_stage; return;
    case State::creating_stage:
        if (!operation()) return;
        open_result = create_exclusive(stage_path.data());
        if (open_result < 0) { error = last_error(); begin_failure(Failure::stage_create); return; }
        descriptor_ = open_result; stage_created_ = true; written_ = 0;
        copy("STAGE WRITE", active_path.data(), active_path.size()); state = State::writing_stage; return;
    case State::writing_stage:
        take_write(selected_.data(), selected_size, State::syncing_stage, Failure::stage_write); return;
    case State::syncing_stage:
        take_sync(State::opening_stage_verify, Failure::stage_sync); return;
    case State::opening_stage_verify:
        copy("STAGE VERIFY", active_path.data(), active_path.size());
        begin_read(stage_path.data(), Buffer::verify, State::reading_stage_verify, Failure::stage_read, max_text_size); return;
    case State::reading_stage_verify:
        take_read(State::validating_stage, Failure::stage_read, Failure::stage_too_large); return;
    case State::validating_stage:
        if (!same(selected_.data(), selected_size, verify_.data(), verify_size)) { begin_failure(Failure::stage_verify); return; }
        copy("MASTER RECHECK", active_path.data(), active_path.size()); state = State::opening_master_recheck; return;
    case State::opening_master_recheck:
        begin_read(master_path, Buffer::verify, State::reading_master_recheck, Failure::master_read, max_text_size); return;
    case State::reading_master_recheck:
        take_read(State::validating_master_recheck, Failure::master_read, Failure::master_too_large); return;
    case State::validating_master_recheck:
        if (!same(original_.data(), original_size, verify_.data(), verify_size)) { begin_failure(Failure::master_changed); return; }
        copy("ATOMIC REPLACE", active_path.data(), active_path.size()); state = State::renaming_stage; return;
    case State::renaming_stage:
        if (!operation()) return;
        rename_result = replace_file(stage_path.data(), master_path);
        if (rename_result != 0) { error = last_error(); begin_failure(Failure::rename); return; }
        stage_created_ = false; master_replaced = true; copy("ACTIVE VERIFY", active_path.data(), active_path.size());
        state = State::opening_active_verify; return;
    case State::opening_active_verify:
        begin_read(master_path, Buffer::verify, State::reading_active_verify, Failure::active_open, max_text_size); return;
    case State::reading_active_verify:
        take_read(State::validating_active, Failure::active_read, Failure::active_too_large); return;
    case State::validating_active:
        if (!same(selected_.data(), selected_size, verify_.data(), verify_size)) { begin_failure(Failure::active_verify); return; }
        copy("LOCK CLEANUP", active_path.data(), active_path.size()); begin_cleanup(State::done); return;
    case State::removing_stage:
        ++operations; unlink_result = remove_file(stage_path.data());
        if (unlink_result != 0 && failure == Failure::none) { error = last_error(); failure = Failure::cleanup; }
        stage_created_ = false; advance_cleanup(); return;
    case State::removing_backup:
        ++operations; unlink_result = remove_file(backup_path.data());
        if (unlink_result != 0 && failure == Failure::none) { error = last_error(); failure = Failure::cleanup; }
        backup_created_ = false; advance_cleanup(); return;
    case State::removing_lock:
        ++operations; unlink_result = remove_file(lock_path);
        if (unlink_result != 0) {
            error = last_error();
            if (failure == Failure::none) failure = Failure::cleanup;
            finish_after_cleanup_ = State::failed;
        }
        lock_owned_ = false; state = finish_after_cleanup_; return;
    case State::opening_final_check:
        begin_read(master_path, Buffer::verify, State::reading_final_check, Failure::final_open, max_text_size); return;
    case State::reading_final_check:
        take_read(State::validating_final_check, Failure::final_read, Failure::final_too_large); return;
    case State::validating_final_check:
        if (!same(selected_.data(), selected_size, verify_.data(), verify_size)) { begin_failure(Failure::final_changed); return; }
        copy("LAUNCH READY", active_path.data(), active_path.size()); state = State::active_ready; return;
    default: return;
    }
}

std::string_view Transaction::summary() const noexcept {
    if (state == State::idle) return "MASTER NOT TOUCHED";
    if (state == State::done)
        return card_backups_ > 0 ? "MASTER APPLIED - VMC CARD BACKED UP" : "MASTER APPLIED VERIFIED AND BACKED UP";
    if (state == State::active_ready) return "ACTIVE MASTER RECHECK PASSED - LAUNCH READY";
    if (busy()) return "TRANSACTION IN PROGRESS - DO NOT CLOSE THE APP";
    switch (failure) {
    case Failure::invalid_request: return "INVALID TESTED PROFILE OR ISO PATH";
    case Failure::operation_limit: return "TRANSACTION OPERATION LIMIT REACHED";
    case Failure::config_open: return "SELECTED CLI OPEN FAILED - MASTER UNCHANGED";
    case Failure::config_read: return "SELECTED CLI READ FAILED - MASTER UNCHANGED";
    case Failure::config_too_large: return "SELECTED CLI TOO LARGE - MASTER UNCHANGED";
    case Failure::config_invalid: return "SELECTED CLI REVALIDATION FAILED - MASTER UNCHANGED";
    case Failure::master_open: return "CURRENT MASTER OPEN FAILED - MASTER UNCHANGED";
    case Failure::master_read: return "CURRENT MASTER READ FAILED - NO REPLACE REQUESTED";
    case Failure::master_too_large: return "CURRENT MASTER TOO LARGE - NO REPLACE REQUESTED";
    case Failure::master_invalid: return "CURRENT MASTER INVALID - NO REPLACE REQUESTED";
    case Failure::card_open: return "VMC CARD OPEN FAILED - MASTER UNCHANGED";
    case Failure::card_create: return "VMC BACKUP CREATE FAILED - MASTER UNCHANGED";
    case Failure::card_read: return "VMC CARD READ FAILED - MASTER UNCHANGED";
    case Failure::card_write: return "VMC BACKUP WRITE FAILED - MASTER UNCHANGED";
    case Failure::card_sync: return "VMC BACKUP SYNC FAILED - MASTER UNCHANGED";
    case Failure::card_close: return "VMC BACKUP CLOSE FAILED - MASTER UNCHANGED";
    case Failure::card_limit: return "VMC BACKUP NAME LIMIT REACHED - MASTER UNCHANGED";
    case Failure::card_too_large: return "VMC CARD TOO LARGE - MASTER UNCHANGED";
    case Failure::lock_create: return "WRITE LOCK EXISTS OR CANNOT BE CREATED";
    case Failure::backup_limit: return "BACKUP NAME LIMIT REACHED - MASTER UNCHANGED";
    case Failure::backup_create: return "BACKUP CREATE FAILED - MASTER UNCHANGED";
    case Failure::backup_write: return "BACKUP WRITE FAILED - MASTER UNCHANGED";
    case Failure::backup_sync: return "BACKUP SYNC FAILED - MASTER UNCHANGED";
    case Failure::backup_read: case Failure::backup_too_large: case Failure::backup_verify:
        return "BACKUP VERIFICATION FAILED - MASTER UNCHANGED";
    case Failure::stage_create: case Failure::stage_write: case Failure::stage_sync:
    case Failure::stage_read: case Failure::stage_too_large: case Failure::stage_verify:
        return "STAGED PROFILE FAILED - MASTER UNCHANGED";
    case Failure::master_changed: return "MASTER CHANGED DURING TRANSACTION - REPLACE CANCELLED";
    case Failure::rename: return "ATOMIC MASTER REPLACE FAILED - BACKUP PRESERVED";
    case Failure::active_open: case Failure::active_read: case Failure::active_too_large:
    case Failure::active_verify: return "MASTER REPLACED BUT READBACK FAILED - DO NOT LAUNCH";
    case Failure::close: return "FILE CLOSE FAILED - DO NOT RETRY UNTIL APP RESTART";
    case Failure::cleanup: return "TRANSACTION CLEANUP FAILED - DO NOT LAUNCH";
    case Failure::final_open: case Failure::final_read: case Failure::final_too_large:
    case Failure::final_changed: return "ACTIVE MASTER CHANGED - LAUNCH CANCELLED";
    case Failure::active_mismatch: return "ACTIVE MASTER DOES NOT MATCH SELECTED PROFILE";
    default: return "TRANSACTION FAILED - DO NOT LAUNCH";
    }
}
} // namespace apply_probe
