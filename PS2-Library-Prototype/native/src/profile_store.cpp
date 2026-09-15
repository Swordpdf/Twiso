// SPDX-License-Identifier: GPL-3.0-or-later
#include "profile_store.hpp"
#include "iso_serial.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ps2lib {
namespace {
struct File {
    int fd = -1;
    explicit File(int value) : fd(value) {}
    ~File() { if (fd >= 0) close(fd); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};
struct Buffer { std::array<char, max_config_size + 1> data{}; std::size_t size = 0; };
Result status(bool ok, const char* message, int code = 0) noexcept {
    Result result{};
    result.ok = ok;
    result.error = code;
    std::snprintf(result.message.data(), result.message.size(), "%s", message);
    return result;
}
bool path(char* target, std::size_t size, const char* prefix, const char* suffix) noexcept {
    const int length = std::snprintf(target, size, "%s%s", prefix, suffix);
    return length > 0 && static_cast<std::size_t>(length) < size;
}
bool read_file(const char* name, Buffer& output) noexcept {
    File file{open(name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK)};
    if (file.fd < 0) return false;
    struct stat info{};
    if (fstat(file.fd, &info) < 0) return false;
    if (!S_ISREG(info.st_mode) || info.st_size <= 0 ||
        static_cast<std::uint64_t>(info.st_size) > max_config_size) { errno = EINVAL; return false; }
    output.size = 0;
    while (output.size < output.data.size()) {
        const auto count = read(file.fd, output.data.data() + output.size,
                                output.data.size() - output.size);
        if (count < 0) { if (errno == EINTR) continue; return false; }
        if (count == 0) break;
        output.size += static_cast<std::size_t>(count);
    }
    if (output.size == 0 || output.size > max_config_size) { errno = EINVAL; return false; }
    return true;
}
bool same(const Buffer& a, const Buffer& b) noexcept {
    return a.size == b.size && std::memcmp(a.data.data(), b.data.data(), a.size) == 0;
}
bool write_new(const char* name, const Buffer& data) noexcept {
    File file{open(name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600)};
    if (file.fd < 0) return false;
    std::size_t written = 0;
    while (written < data.size) {
        const auto count = write(file.fd, data.data.data() + written, data.size - written);
        if (count < 0) { if (errno == EINTR) continue; return false; }
        if (count == 0) { errno = EIO; return false; }
        written += static_cast<std::size_t>(count);
    }
    if (fsync(file.fd) < 0) return false;
    const int descriptor = file.fd;
    file.fd = -1;
    return close(descriptor) == 0;
}
std::string_view unquote(std::string_view value) noexcept {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.remove_prefix(1); value.remove_suffix(1);
    }
    return value;
}
Result inspect(const Profile& profile, const char* prefix, Buffer& selected,
               Buffer& current, char* master, std::size_t master_size) noexcept {
    std::array<char, 768> config{}, item{};
    std::array<char, 256> relative{};
    const int count = std::snprintf(relative.data(), relative.size(),
        "/data/PS2/library/profiles/%s", profile.config_name);
    if (count < 0 || static_cast<std::size_t>(count) >= relative.size() ||
        !path(config.data(), config.size(), prefix, relative.data()) ||
        !path(master, master_size, prefix, "/data/PS2/configs/master.txt"))
        return status(false, "PATH TOO LONG");
    if (!read_file(master, current)) return status(false, "CANNOT READ CURRENT MASTER OR DATA MOUNT", errno);
    if (!read_file(config.data(), selected)) return status(false, "CANNOT READ SELECTED PROFILE", errno);
    auto checked = validate_config({selected.data.data(), selected.size}, profile);
    if (!checked.ok) return checked;
    if (profile.image[0] != '\0') {
        if (!path(item.data(), item.size(), prefix, profile.image)) return status(false, "ISO PATH TOO LONG");
        File iso{open(item.data(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK)};
        if (iso.fd < 0) return status(false, "ISO IS MISSING OR NOT READABLE", errno);
        struct stat info{};
        if (fstat(iso.fd, &info) < 0) return status(false, "CANNOT CHECK ISO SIZE", errno);
        if (!S_ISREG(info.st_mode) || static_cast<std::uint64_t>(info.st_size) != profile.image_size)
            return status(false, "ISO SIZE DOES NOT MATCH TESTED SOURCE");
        std::array<char, 11> serial{};
        if (!read_iso_serial(item.data(), serial) || std::strcmp(serial.data(), profile.serial) != 0)
            return status(false, "ISO BOOT SERIAL DOES NOT MATCH PROFILE");
    }
    if (profile.lua[0] != '\0') {
        if (!path(item.data(), item.size(), prefix, profile.lua)) return status(false, "LUA PATH TOO LONG");
        Buffer lua{};
        if (!read_file(item.data(), lua)) return status(false, "LUA IS MISSING EMPTY OR TOO LARGE", errno);
    }
    return status(true, "PROFILE AND REQUIRED FILES CHECKED");
}
}

Result validate_config(std::string_view text, const Profile& profile) noexcept {
    if (text.empty() || text.size() > max_config_size) return status(false, "PROFILE SIZE INVALID");
    for (const unsigned char c : text)
        if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c > 126)
            return status(false, "PROFILE MUST BE ASCII WITHOUT BOM");
    std::array<std::string_view, 128> keys{};
    std::size_t used = 0;
    bool title = false, image = false, lua = false, vmc = false, rom = false, discs = false;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = std::string_view{text.data(), end == std::string_view::npos ? text.size() : end};
        text = end == std::string_view::npos ? std::string_view{} :
            std::string_view{text.data() + end + 1, text.size() - end - 1};
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto equal = line.find('=');
        if (!line.starts_with("--") || equal == std::string_view::npos || equal < 3)
            return status(false, "PROFILE LINE IS NOT A CLI OPTION");
        const auto key = std::string_view{line.data(), equal};
        auto value = std::string_view{line.data() + equal + 1, line.size() - equal - 1};
        for (auto prior : keys) if (prior == key) return status(false, "DUPLICATE CLI OPTION");
        if (used == keys.size()) return status(false, "TOO MANY CLI OPTIONS");
        keys[used++] = key;
        value = unquote(value);
        if (key == "--config" || key == "--path-patches" || key == "--path-featuredata" ||
            key == "--load-feature-lua" || key == "--load-tooling-lua" || key.starts_with("--image-disc"))
            return status(false, "EXTRA CONFIG OR DISC LAYERS NOT ALLOWED IN PROTOTYPE");
        if (key == "--ps2-title-id") { title = value == profile.serial; if (!title) return status(false, "WRONG PS2 SERIAL"); }
        if (key == "--image") { image = value == profile.image && profile.image[0]; if (!image) return status(false, "WRONG ISO PATH"); }
        if (key == "--config-local-lua") { lua = value == profile.lua; if (!lua) return status(false, "WRONG LUA PATH"); }
        if (key == "--path-vmc") { vmc = value == "/tmp/vmc"; if (!vmc) return status(false, "MEMORY CARD PATH CHANGES DISABLED"); }
        if (key == "--rom") { rom = value == "PS20220WD20050620.crack"; if (!rom) return status(false, "WRONG ROM SETTING"); }
        if (key == "--max-disc-num") { discs = value == "1"; if (!discs) return status(false, "ONLY ONE DISC PER PROFILE"); }
        if (profile.image[0] == '\0' && key == "--ee-hook") return status(false, "GAME PATCH IN HOME BREW PROFILE");
    }
    if (!title || !lua || !vmc || !rom || !discs || (profile.image[0] != '\0' && !image))
        return status(false, "PROFILE IS MISSING REQUIRED BOOT SETTINGS");
    return status(true, "CONFIG VALIDATED");
}

Result validate(const Profile& profile, const char* prefix) noexcept {
    Buffer selected{}, current{};
    std::array<char, 768> master{};
    return inspect(profile, prefix, selected, current, master.data(), master.size());
}

Result validate_active(const Profile& profile, const char* prefix) noexcept {
    Buffer selected{}, current{};
    std::array<char, 768> master{};
    auto result = inspect(profile, prefix, selected, current, master.data(), master.size());
    if (!result.ok) return result;
    if (!same(selected, current)) return status(false, "MASTER CHANGED - APPLY PROFILE AGAIN BEFORE LAUNCH");
    return status(true, "ACTIVE PROFILE CHECKED");
}

Result apply(const Profile& profile, const char* prefix) noexcept {
    Buffer selected{}, current{}, verify{};
    std::array<char, 768> master{}, backup{}, temporary{}, lock{};
    auto result = inspect(profile, prefix, selected, current, master.data(), master.size());
    if (!result.ok) return result;
    if (!path(lock.data(), lock.size(), prefix, "/data/PS2/configs/.library-write.lock"))
        return status(false, "LOCK PATH TOO LONG");
    File guard{open(lock.data(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600)};
    if (guard.fd < 0) return status(false, "WRITE LOCK EXISTS OR DIRECTORY NOT WRITABLE", errno);
    struct Unlock { const char* name; ~Unlock() { unlink(name); } } unlock{lock.data()};
    // All transaction names are exclusive; never overwrite an older backup.
    bool reserved = false;
    for (unsigned index = 1; index <= 9999; ++index) {
        const int size = std::snprintf(backup.data(), backup.size(), "%s.library-backup-%04u", master.data(), index);
        if (size < 0 || static_cast<std::size_t>(size) >= backup.size())
            return status(false, "BACKUP PATH TOO LONG MASTER UNCHANGED");
        if (write_new(backup.data(), current)) { reserved = true; break; }
        if (errno != EEXIST) return status(false, "BACKUP WRITE FAILED MASTER UNCHANGED", errno);
    }
    if (!reserved) return status(false, "BACKUP LIMIT REACHED MASTER UNCHANGED");
    if (!read_file(backup.data(), verify) || !same(current, verify))
        return status(false, "BACKUP VERIFICATION FAILED MASTER UNCHANGED", errno);
    const int size = std::snprintf(temporary.data(), temporary.size(), "%s.pending", backup.data());
    if (size < 0 || static_cast<std::size_t>(size) >= temporary.size())
        return status(false, "STAGING PATH TOO LONG MASTER UNCHANGED");
    if (!write_new(temporary.data(), selected)) return status(false, "STAGING WRITE FAILED MASTER UNCHANGED", errno);
    if (!read_file(temporary.data(), verify) || !same(selected, verify))
        return status(false, "STAGING VERIFICATION FAILED MASTER UNCHANGED", errno);
    if (!read_file(master.data(), verify) || !same(current, verify))
        return status(false, "MASTER CHANGED DURING SELECTION ABORTED", errno);
    if (rename(temporary.data(), master.data()) < 0)
        return status(false, "ATOMIC REPLACE FAILED MASTER UNCHANGED", errno);
    result = status(true, "PROFILE APPLIED BACKUP SAVED");
    std::snprintf(result.backup.data(), result.backup.size(), "%s", backup.data());
    if (!read_file(master.data(), verify) || !same(selected, verify)) {
        result.ok = false;
        std::snprintf(result.message.data(), result.message.size(), "%s",
                      "MASTER REPLACED BUT READBACK FAILED DO NOT LAUNCH");
    }
    return result;
}
}
