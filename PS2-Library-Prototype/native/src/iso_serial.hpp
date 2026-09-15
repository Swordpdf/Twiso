// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
namespace ps2lib {
// Reads only ISO9660 metadata and SYSTEM.CNF, never hashes/copies the game.
bool read_iso_serial(const char* path, std::array<char, 11>& serial) noexcept;
}
