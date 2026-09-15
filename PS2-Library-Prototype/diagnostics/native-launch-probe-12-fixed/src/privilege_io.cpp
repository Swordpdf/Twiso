// PS5 libc boundary for etaHEN/OnionHEN request. SPDX-License-Identifier: GPL-3.0-or-later
#include "privilege_request.hpp"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace privilege_probe {
int process_id() noexcept { return static_cast<int>(getpid()); }
int effective_user_id() noexcept { return static_cast<int>(geteuid()); }
int remove_request(const char* path) noexcept {
    if (unlink(path) == 0 || errno == ENOENT) return 0;
    return -1;
}
int open_request(const char* path) noexcept {
    return open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
}
int make_request_readable(int descriptor) noexcept { return fchmod(descriptor, 0666); }
int write_request(int descriptor, const void* data, std::size_t size) noexcept {
    return static_cast<int>(write(descriptor, data, size));
}
int sync_request(int descriptor) noexcept { return fsync(descriptor); }
int close_request(int descriptor) noexcept { return close(descriptor); }
int publish_request(const char* source, const char* target) noexcept { return rename(source, target); }
int request_exists(const char* path) noexcept { return access(path, F_OK) == 0 ? 0 : -1; }
int last_error() noexcept { return errno; }
}
