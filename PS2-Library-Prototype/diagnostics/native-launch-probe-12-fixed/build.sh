#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Local isolated build only. No FTP, console mutation, or app launch.
set -euo pipefail
# Host sanitizer binaries reserve a large entry frame (over-aligned test
# objects plus ASan redzones); constrained shells default to 8 MiB and trip
# the guard before the first check. The console product keeps these objects
# global, so this only affects local test runs.
ulimit -s unlimited 2>/dev/null || ulimit -s 65536 2>/dev/null || true
probe=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
prototype=$(cd -- "$probe/../.." && pwd)
native="$prototype/native"
baseline="$prototype/diagnostics/native-menu-probe-02"
working="$prototype/diagnostics/native-menu-probe-02-aligned"
serial="$prototype/diagnostics/native-serial-probe-07"
profile="$prototype/diagnostics/native-profile-probe-08"
sdk="$native/.deps/native/ps5-payload-sdk"
tool="$probe/build/ps5-native-tool"
app="${PS2_LIBRARY_APP_DIR:-$probe/dist/PPSA99202}"
[[ ! -e "$app" ]] || { echo "Preserving existing Probe 12 app output." >&2; exit 2; }
mkdir -p "$probe/build/obj"

native_defines=()
if [[ "${PS2_LIBRARY_SHARED_VMC:-0}" == "1" ]]; then
    native_defines+=(-DPS2_LIBRARY_SHARED_VMC=1)
fi

g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$profile/src" "$probe/apply_tests.cpp" "$probe/src/apply_store.cpp" \
    "$profile/src/profile_check.cpp" "$profile/src/profile_io.cpp" -o "$probe/build/apply-tests"
"$probe/build/apply-tests"
if [[ "${PS2_LIBRARY_SHARED_VMC:-0}" == "1" ]]; then
    g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
        -DPS2_LIBRARY_SHARED_VMC=1 -I "$profile/src" "$probe/apply_tests.cpp" \
        "$probe/src/apply_store.cpp" "$profile/src/profile_check.cpp" \
        "$profile/src/profile_io.cpp" -o "$probe/build/apply-tests-shared-vmc"
    "$probe/build/apply-tests-shared-vmc"
fi
g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$probe/src" "$probe/privilege_tests.cpp" "$probe/src/privilege_request.cpp" \
    -o "$probe/build/privilege-tests"
"$probe/build/privilege-tests"
g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$probe/src" "$probe/gamefiles_tests.cpp" "$probe/src/gamefiles.cpp" \
    -o "$probe/build/gamefiles-tests"
"$probe/build/gamefiles-tests"
g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$baseline/src" -I "$serial/src" -I "$profile/src" -I "$probe/src" \
    "$probe/control_tests.cpp" "$probe/src/apply_store.cpp" "$probe/src/gamefiles.cpp" "$probe/src/privilege_request.cpp" "$profile/src/profile_check.cpp" \
    "$serial/src/iso_directory.cpp" "$serial/src/serial_scan.cpp" -o "$probe/build/control-tests"
"$probe/build/control-tests"
"$profile/build/profile-tests"
"$serial/build/host-tests"
"$serial/build/serial-tests"
"$baseline/build/host-tests"
python3 "$working/test_layout.py"
g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -ffunction-sections -fdata-sections -I "$profile/src" "$profile/artifact_tests.cpp" \
    "$profile/src/profile_check.cpp" -Wl,--gc-sections -o "$probe/build/external-artifact-tests"
"$probe/build/external-artifact-tests" \
    "$profile/upload/data/PS2/library/catalog/SCUS-97353.profile" \
    "$profile/upload/data/PS2/library/profiles/rac3-library.txt" \
    "$prototype/../rac3-external-test/upload/data/PS2/configs/rac3.lua"

previous_tool="$prototype/diagnostics/native-iso-probe-03/build/ps5-native-tool"
printf '%s  %s\n' '82ea9a128fd9dfb674911465e9372dd4709f1eaa0c88033287fcd5bdfa185974' "$previous_tool" | sha256sum --check --strict
cp "$previous_tool" "$tool"

objects=()
for item in main apply_store apply_io gamefiles privilege_request privilege_io cover_store cover_fetch jpeg_decode; do
    source="$probe/src/$item.cpp"; object="$probe/build/obj/$item.o"
    # gamefiles hand-rolls copies so the container gate sees no memmove/memcpy;
    # keep the optimizer from re-synthesizing those calls in this TU only.
    extra_flags=()
    if [[ "$item" == "gamefiles" ]]; then
        extra_flags+=(-fno-builtin-memmove -fno-builtin-memcpy)
    fi
    PS5_PAYLOAD_SDK="$sdk" sh "$native/tooling/prospero-clang18" \
        -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
        "${native_defines[@]}" "${extra_flags[@]}" \
        -I "$baseline/src" -I "$serial/src" -I "$profile/src" -I "$probe/src" \
        -ffunction-sections -fdata-sections -c "$source" -o "$object"
    objects+=("$object")
done
for item in profile_check profile_io; do
    source="$profile/src/$item.cpp"; object="$probe/build/obj/$item.o"
    PS5_PAYLOAD_SDK="$sdk" sh "$native/tooling/prospero-clang18" \
        -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
        "${native_defines[@]}" \
        -I "$profile/src" -ffunction-sections -fdata-sections -c "$source" -o "$object"
    objects+=("$object")
done
for item in iso_directory directory_io serial_scan serial_io; do
    source="$serial/src/$item.cpp"; object="$probe/build/obj/$item.o"
    PS5_PAYLOAD_SDK="$sdk" sh "$native/tooling/prospero-clang18" \
        -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
        "${native_defines[@]}" \
        -I "$serial/src" -ffunction-sections -fdata-sections -c "$source" -o "$object"
    objects+=("$object")
done
PS5_PAYLOAD_SDK="$sdk" sh "$native/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
    "${native_defines[@]}" \
    -I "$baseline/src" -ffunction-sections -fdata-sections \
    -c "$baseline/src/demo_renderer.cpp" -o "$probe/build/obj/demo_renderer.o"
objects+=("$probe/build/obj/demo_renderer.o")
PS5_PAYLOAD_SDK="$sdk" sh "$native/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
    "${native_defines[@]}" \
    -I "$baseline/src" -ffunction-sections -fdata-sections \
    -c "$baseline/src/ui_font.cpp" -o "$probe/build/obj/ui_font.o"
objects+=("$probe/build/obj/ui_font.o")
PS5_PAYLOAD_SDK="$sdk" sh "$native/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
    "${native_defines[@]}" \
    -I "$baseline/src" -ffunction-sections -fdata-sections \
    -c "$baseline/src/ui_icons.cpp" -o "$probe/build/obj/ui_icons.o"
objects+=("$probe/build/obj/ui_icons.o")
for item in app_crt app_cpp_runtime; do
    PS5_PAYLOAD_SDK="$sdk" sh "$native/tooling/prospero-clang18" \
        -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
        "${native_defines[@]}" \
        -ffunction-sections -fdata-sections -c "$native/tooling/native/$item.cpp" -o "$probe/build/obj/$item.o"
done
ld.lld-18 -m elf_x86_64 -pie -z max-page-size=0x4000 -mllvm -emulated-tls \
    --hash-style=gnu -T "$native/tooling/native/ps5-pie.ld" --eh-frame-hdr \
    --version-script "$native/tooling/native/app-symbols.map" -e _start \
    -o "$probe/build/llvm-pie.elf" "$probe/build/obj/app_crt.o" \
    "$probe/build/obj/app_cpp_runtime.o" "${objects[@]}" --as-needed "$sdk"/target/lib/*.so
"$tool" link --in "$probe/build/llvm-pie.elf" --out "$probe/build/eboot.elf" \
    --stub-dir "$sdk/target/lib" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf
python3 "$probe/prepare_console_elf.py" --input "$probe/build/eboot.elf" \
    --output "$probe/build/eboot-console-ready.elf"
python3 "$probe/verify_build.py"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/assets"
"$tool" self --sign --in "$probe/build/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
(cd "$native/runtime" && sha256sum --check --strict libc.prx.sha256)
# Twiso app identity/presentation: tracked source in app-src (verified working
# 2026-09-14 dist). libc.prx stays generated: copied from native/runtime after
# the sha256 gate above, never from the untracked probe-08 dist-final.
for name in sce_sys/param.json sce_sys/icon0.png sce_sys/pic0.png sce_sys/pic1.png assets/banner.txt; do
    cp "$probe/app-src/PPSA99202/$name" "$app/$name"
    cmp "$probe/app-src/PPSA99202/$name" "$app/$name"
done
cp "$native/runtime/libc.prx" "$app/sce_module/libc.prx"
cmp "$native/runtime/libc.prx" "$app/sce_module/libc.prx"
"$tool" self --inspect --file "$app/eboot.bin"
"$tool" self --inspect --file "$app/sce_module/libc.prx"
"$probe/preview.sh"
printf 'Post-jailbreak SystemService polished settings UI built locally: %s\n' "$app"

