// SPDX-License-Identifier: GPL-3.0-or-later
#include "serial_scan.hpp"
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

namespace serial_probe {
int open_iso(const char* path) noexcept { return ::open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK); }
std::int64_t seek_iso(int descriptor, std::uint64_t offset) noexcept {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        errno = EOVERFLOW; return -1;
    }
    return ::lseek(descriptor, static_cast<off_t>(offset), SEEK_SET);
}
int read_iso(int descriptor, void* buffer, std::size_t size) noexcept {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        errno = EINVAL; return -1;
    }
    return static_cast<int>(::read(descriptor, buffer, size));
}
int close_iso(int descriptor) noexcept { return ::close(descriptor); }
int last_error() noexcept { return errno; }
} // namespace serial_probe
