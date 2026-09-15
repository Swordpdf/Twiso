// SPDX-License-Identifier: GPL-3.0-or-later
#include "profile_check.hpp"
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

namespace profile_probe {
int open_file(const char* path) noexcept { return ::open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK); }
std::int64_t seek_file_end(int descriptor) noexcept {
    const auto result = ::lseek(descriptor, 0, SEEK_END);
    if (result > std::numeric_limits<std::int64_t>::max()) { errno = EOVERFLOW; return -1; }
    return static_cast<std::int64_t>(result);
}
int read_file(int descriptor, void* buffer, std::size_t size) noexcept {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        errno = EINVAL; return -1;
    }
    return static_cast<int>(::read(descriptor, buffer, size));
}
int close_file(int descriptor) noexcept { return ::close(descriptor); }
int last_error() noexcept { return errno; }
} // namespace profile_probe
