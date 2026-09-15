// SPDX-License-Identifier: GPL-3.0-or-later
#include "apply_store.hpp"
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

namespace apply_probe {
int open_read_only(const char* path) noexcept { return ::open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK); }
int create_exclusive(const char* path) noexcept {
    return ::open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
}
int read_data(int descriptor, void* buffer, std::size_t size) noexcept {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) { errno = EINVAL; return -1; }
    return static_cast<int>(::read(descriptor, buffer, size));
}
int write_data(int descriptor, const void* buffer, std::size_t size) noexcept {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) { errno = EINVAL; return -1; }
    return static_cast<int>(::write(descriptor, buffer, size));
}
int sync_data(int descriptor) noexcept { return ::fsync(descriptor); }
int close_data(int descriptor) noexcept { return ::close(descriptor); }
int replace_file(const char* source, const char* target) noexcept { return ::rename(source, target); }
int remove_file(const char* path) noexcept { return ::unlink(path); }
int last_error() noexcept { return errno; }
} // namespace apply_probe
