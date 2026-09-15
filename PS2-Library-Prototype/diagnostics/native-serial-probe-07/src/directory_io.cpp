// SPDX-License-Identifier: GPL-3.0-or-later
#include "iso_directory.hpp"
#include <cstddef>
#include <fcntl.h>
#include <sys/dirent.h>

// Public PS5 ABI usage: PS5Dev/Byepervisor src/self.cpp (see README).
extern "C" int sceKernelOpen(const char*, int, int);
extern "C" int sceKernelGetdents(int, char*, int);
extern "C" int sceKernelClose(int);

// Do not silently compile the parser against a different directory-entry ABI.
static_assert(offsetof(dirent, d_fileno) == 0 && sizeof(dirent::d_fileno) == 4);
static_assert(offsetof(dirent, d_reclen) == 4 && sizeof(dirent::d_reclen) == 2);
static_assert(offsetof(dirent, d_type) == 6 && offsetof(dirent, d_namlen) == 7);
static_assert(offsetof(dirent, d_name) == 8 && sizeof(dirent::d_name) == 256);
static_assert(DT_REG == 8 && DT_UNKNOWN == 0);
constexpr int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW;
static_assert((flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) == 0);

namespace iso_probe {
int open_directory() noexcept { return sceKernelOpen(directory, flags, 0); }
int read_directory(int descriptor, char* buffer, int size) noexcept {
    return sceKernelGetdents(descriptor, buffer, size);
}
int close_directory(int descriptor) noexcept { return sceKernelClose(descriptor); }
} // namespace iso_probe
