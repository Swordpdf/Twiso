// On-demand PS2 cover art store. SPDX-License-Identifier: GPL-3.0-or-later
#include "cover_store.hpp"

#include <fcntl.h>
#include <unistd.h>

namespace cover_probe {
namespace {
bool valid_serial(std::string_view serial) noexcept {
    if (serial.empty() || serial.size() > max_serial)
        return false;
    for (const char c : serial) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '-';
        if (!ok)
            return false;
    }
    return true;
}
} // namespace

bool Store::busy() const noexcept {
    return state == State::opening || state == State::reading || state == State::decoding;
}

void Store::close_current() noexcept {
    if (descriptor_ >= 0) {
        (void)::close(descriptor_);
        descriptor_ = -1;
    }
}

void Store::begin_failure(Failure reason) noexcept {
    close_current();
    failure = reason;
    state = State::failed;
}

bool Store::clear() noexcept {
    close_current();
    state = State::idle;
    failure = Failure::none;
    width = 0;
    height = 0;
    jpeg_size_ = 0;
    operations = 0;
    return true;
}

bool Store::request_file(std::string_view path) noexcept {
    if (busy() || path.empty() || path.size() >= jpeg_path_.size())
        return false;
    close_current();
    failure = Failure::none;
    width = 0;
    height = 0;
    jpeg_size_ = 0;
    operations = 0;
    std::size_t at = 0;
    for (const char c : path)
        jpeg_path_[at++] = c;
    jpeg_path_[at] = 0;
    state = State::opening;
    return true;
}

bool Store::request(std::string_view serial) noexcept {
    if (busy())
        return false;
    if (!valid_serial(serial)) {
        close_current();
        failure = Failure::invalid_serial;
        width = 0;
        height = 0;
        jpeg_size_ = 0;
        state = State::failed;
        return true;
    }
    std::array<char, max_path> path{};
    std::size_t at = 0;
    for (const char c : std::string_view{cover_dir})
        path[at++] = c;
    for (const char c : serial)
        path[at++] = c;
    for (const char c : std::string_view{".jpg"})
        path[at++] = c;
    path[at] = 0;
    return request_file({path.data(), at});
}

void Store::step() noexcept {
    switch (state) {
    case State::opening: {
        ++operations;
        descriptor_ = ::open(jpeg_path_.data(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
        if (descriptor_ < 0) {
            // A missing cover is an expected state, not an error.
            state = State::missing;
            return;
        }
        state = State::reading;
        return;
    }
    case State::reading: {
        ++operations;
        if (jpeg_size_ >= jpeg_.size()) {
            begin_failure(Failure::too_large);
            return;
        }
        const std::size_t room = jpeg_.size() - jpeg_size_;
        const std::size_t want = room < read_chunk ? room : read_chunk;
        const long got = ::read(descriptor_, jpeg_.data() + jpeg_size_, want);
        if (got < 0) {
            begin_failure(Failure::read);
            return;
        }
        if (got == 0) {
            close_current();
            if (jpeg_size_ == 0) {
                begin_failure(Failure::empty);
                return;
            }
            state = State::decoding;
            return;
        }
        jpeg_size_ += static_cast<std::size_t>(got);
        return;
    }
    case State::decoding: {
        ++operations;
        jpeg_probe::Image image;
        const auto result = jpeg_probe::decode({jpeg_.data(), jpeg_size_}, pixels_, image);
        if (result != jpeg_probe::Result::ok) {
            begin_failure(Failure::decode);
            return;
        }
        width = image.width;
        height = image.height;
        jpeg_size_ = 0;
        state = State::done;
        return;
    }
    case State::idle:
    case State::done:
    case State::missing:
    case State::failed:
        return;
    }
}

std::string_view Store::summary() const noexcept {
    switch (state) {
    case State::idle:
        return "NO COVER LOADED";
    case State::opening:
        return "OPENING COVER";
    case State::reading:
        return "READING COVER";
    case State::decoding:
        return "DECODING COVER";
    case State::done:
        return "COVER READY";
    case State::missing:
        return "NO COVER ON DISK";
    case State::failed:
        switch (failure) {
        case Failure::invalid_serial:
            return "COVER: BAD GAME ID";
        case Failure::open:
            return "COVER: OPEN FAILED";
        case Failure::read:
            return "COVER: READ FAILED";
        case Failure::too_large:
            return "COVER: FILE TOO LARGE";
        case Failure::empty:
            return "COVER: FILE EMPTY";
        case Failure::decode:
            return "COVER: DECODE FAILED";
        case Failure::none:
            break;
        }
        return "COVER: FAILED";
    }
    return "COVER: UNKNOWN";
}

std::span<const std::uint8_t> Store::pixels() const noexcept {
    if (state != State::done || width <= 0 || height <= 0)
        return {};
    return {pixels_.data(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4};
}
} // namespace cover_probe
