#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
probe=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
baseline="$probe/../native-menu-probe-02"
serial="$probe/../native-serial-probe-07"
profile="$probe/../native-profile-probe-08"
g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -I "$baseline/src" -I "$serial/src" -I "$profile/src" -I "$probe/src" \
    "$probe/preview.cpp" "$serial/src/iso_directory.cpp" "$serial/src/serial_scan.cpp" \
    "$profile/src/profile_check.cpp" "$probe/src/apply_store.cpp" "$probe/src/gamefiles.cpp" "$probe/src/privilege_request.cpp" \
    "$probe/src/cover_store.cpp" "$probe/src/cover_fetch.cpp" "$probe/src/jpeg_decode.cpp" \
    "$baseline/src/ui_font.cpp" "$baseline/src/ui_icons.cpp" \
    -Wl,--gc-sections -o "$probe/build/preview-host"
(cd "$probe" && ./build/preview-host)
