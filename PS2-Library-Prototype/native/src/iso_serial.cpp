// SPDX-License-Identifier: GPL-3.0-or-later
#include "iso_serial.hpp"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace ps2lib {
namespace {
std::uint32_t le32(const unsigned char* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
        (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
bool read_at(int fd, std::uint64_t offset, void* target, std::size_t size) noexcept {
    if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) < 0) return false;
    auto* out = static_cast<unsigned char*>(target);
    while (size) {
        auto n = read(fd, out, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        out += n; size -= static_cast<std::size_t>(n);
    }
    return true;
}
bool digit(char c) noexcept { return c >= '0' && c <= '9'; }
bool upper(char c) noexcept { return c >= 'A' && c <= 'Z'; }
}
bool read_iso_serial(const char* path, std::array<char, 11>& serial) noexcept {
    serial.fill(0);
    const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return false;
    struct Close { int fd; ~Close() { close(fd); } } close_file{fd};
    std::array<unsigned char, 2048> pvd{};
    bool found = false;
    for (unsigned sector = 16; sector < 48; ++sector) {
        if (!read_at(fd, std::uint64_t(sector) * 2048, pvd.data(), pvd.size()) ||
            std::memcmp(pvd.data() + 1, "CD001", 5) != 0) return false;
        if (pvd[0] == 255) return false;
        if (pvd[0] == 1) { found = true; break; }
    }
    if (!found || pvd[128] != 0 || pvd[129] != 8 || pvd[156] < 34) return false;
    const std::uint64_t root_offset = std::uint64_t(le32(pvd.data() + 158)) * 2048;
    const std::uint32_t root_size = le32(pvd.data() + 166);
    if (!root_size || root_size > 1024 * 1024) return false;
    std::array<unsigned char, 256> record{};
    for (std::uint32_t at = 0; at < root_size;) {
        if (!read_at(fd, root_offset + at, record.data(), 1)) return false;
        const unsigned size = record[0];
        if (!size) { at = (at / 2048 + 1) * 2048; continue; }
        if (size < 34 || at + size > root_size ||
            !read_at(fd, root_offset + at, record.data(), size)) return false;
        const unsigned name_size = record[32];
        if (33 + name_size > size) return false;
        if (!(record[25] & 2) &&
            ((name_size == 12 && std::memcmp(record.data() + 33, "SYSTEM.CNF;1", 12) == 0) ||
             (name_size == 10 && std::memcmp(record.data() + 33, "SYSTEM.CNF", 10) == 0))) {
            const auto length = le32(record.data() + 10);
            if (!length || length > 4096) return false;
            std::array<char, 4097> cnf{};
            if (!read_at(fd, std::uint64_t(le32(record.data() + 2)) * 2048, cnf.data(), length)) return false;
            if (std::memchr(cnf.data(), '\0', length)) return false;
            // Match the boot executable's normal Sxxx_ddd.dd disc serial.
            // Do not infer a serial from the ISO filename or game title.
            const char* boot = std::strstr(cnf.data(), "BOOT2");
            if (!boot) return false;
            const char* end = std::strchr(boot, '\n');
            if (!end) end = cnf.data() + length;
            for (const char* p = boot; p + 11 <= end; ++p) {
                if (p[0] == 'S' && upper(p[1]) && upper(p[2]) && upper(p[3]) &&
                    p[4] == '_' && digit(p[5]) && digit(p[6]) && digit(p[7]) &&
                    p[8] == '.' && digit(p[9]) && digit(p[10])) {
                    std::memcpy(serial.data(), p, 4); serial[4] = '-';
                    std::memcpy(serial.data() + 5, p + 5, 3);
                    std::memcpy(serial.data() + 8, p + 9, 2);
                    return true;
                }
            }
            return false;
        }
        at += size;
    }
    return false;
}
}
