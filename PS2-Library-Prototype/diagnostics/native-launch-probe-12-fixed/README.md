# PS2 Library Probe 16

Probe 16 keeps Probe 12's loader-compatible launch imports and adds a fast
console path. After the user selects an ISO and presses Cross once, the app
automatically performs the read-only profile check, skips the write when the
active master already matches, applies a bounded transaction only when the
selected ISO differs, requests OnionHEN once, and launches the mapped PS4
backend when the request is consumed. Manual confirmation screens remain compiled
for host tests and for any non-fast build.

The menu lists six backend packages: WotM v1, JAK2 v2, RECVX, Red Faction,
KOF2000, and Rewind. The entries use title IDs `LIBR12347` through
`LIBR12352` and point at the zero-ISO packages under
`emu-pkgs/backends-zero-iso/`. D-pad Up/Down moves focus through every row; X
selects the backend, toggles an option, or confirms the ISO/launch action.
Circle stays on the settings page and Triangle resets it.
Widescreen and upscaling are applied during the verified transaction by
rewriting `--host-display-mode`, `--gs-uprender`, and `--gs-upscale` in the
staged master. Scanlines, progressive/deinterlace, and bilinear remain UI
preview flags until their exact backend syntax is verified.
All startup, scan, apply, and privilege wait states display a segmented
8-bit green loading bar instead of the previous code/progress block.

The default build preserves the validated `--path-vmc="/tmp/vmc"` line. The
`PS2_LIBRARY_SHARED_VMC=1` build in `dist-vmc-etahen-cardbackup/PPSA99202` enables the
automatic cross-backend save bridge: at apply time it rewrites that line to
`--path-vmc="/data/PS2/saves/<disc-serial>"`. Because the path is derived from
the PS2 disc serial rather than the backend package title ID, WotM, JAK2, and
the other backend packages can see the same raw PS2 VMC for a given game.
Create `/data/PS2/saves/<disc-serial>` once for each game (for example
`/data/PS2/saves/SLES-52017`) before first launch. The emulator can then create
`VMC0.card` there and reuse it on later backend launches.
Before the master is touched, each existing shared slot (`VMC0.card`,
`VMC1.card`) is copied to a never-overwritten `VMC?.card.backup-NNNN` file in
the same serial directory (Garlic-style safety for raw cards; absent cards are
skipped, read-only checks never back up). The privilege request in this build
targets etaHEN (`/download0/etahen_jailbreak`), which OnionHEN also consumes.

This is automatic persistence, not a decryptor for old PS5 native saves. The
existing encrypted containers under each PS4 title ID are left untouched; a
first run through the bridge starts with a new external card unless a raw VMC
is copied in through a separate, verified tool. Do not copy `sdimg_*` or `.bin`
files between title IDs.

Probe 12 fixes the exact post-jailbreak crash captured from Probe 11. OnionHEN
successfully granted PID 300 full privileges, but the next launch confirmation
produced a repeatable `SIGSEGV` with `RIP=0`. The captured return address maps to
`launch_backend()`, and the preceding instruction calls the unresolved
`sceLncUtilLaunchApp` import through a null slot.

Probe 12 preserves Probe 11's confirmed readable request protocol: stale-file
cleanup, a mode-`0666` staged request, and atomic publication. Once the request
is consumed, it uses the already-resolved `sceSystemServiceLaunchApp` wrapper from
Probe 09. That wrapper reached LncService before jailbreak but received
`0x8094000F` because the caller was not a system process; Probe 12 retests the
same wrapper only after OnionHEN has changed the caller credentials.

The optional `dist-as-is/PPSA99202` build contains the same launch path plus
the external-profile parser's explicit `status=as-is` mode. An as-is profile
must provide a WotM CLI file, use `lua=-`, and may set `size=0` when the ISO
size is not available to the host-side staging step. The original `dist/`
Probe 12 app is preserved unchanged.

## Per-game files (serial-keyed)

Launching is driven by two files per game under `/data/PS2/configs/`, named
after the scanned game serial: `<SERIAL>.txt` (CLI) and `<SERIAL>.lua`.
Renaming an ISO never orphans them. The game TXT must set `--image` to the
selected ISO, `--ps2-title-id` to the serial, and `--path-vmc` to
`/data/PS2/saves/<serial>`; it must not contain `--config*` lines (the Lua is
selected by the master). Display and tuning lines are yours to edit.

If either file is absent, the menu stops on a prompt instead of launching:
place your own files over FTP (`TRIANGLE` rechecks), or press `CROSS` once to
create working defaults (as-is CLI shape plus a no-op Lua). Existing files are
never modified or overwritten by the menu. `master.txt` is then replaced with
a two-line loader pointing at those files, using the existing transactional
backup/verify flow (including the shared-VMC card backup).

## Prerequisite

etaHEN (or OnionHEN) App jailbreak must be enabled and `PPSA99202` must be
present in `[app_jailbreak] exact_title_ids` before PS2 Library starts. This
build publishes the documented request `{"PID":<current pid>}` at
`/download0/etahen_jailbreak`, which both HENs consume; it does not alter HEN
configuration.

The patch bundle that grants `/data` access must remain active. It covers the
external ISO/config paths, while the HEN supplies the process credentials
tested by the launch stage.

## Console flow

The menu is three boxes plus launch: games, backends, config checkboxes,
then a launch button. Up/Down moves through every row, Left/Right also
cycles the backend, Cross activates the focused row, Triangle rescans,
Circle goes back.

1. Fully close PS2 Library and any previously running PS2 backend.
2. Start klogsrv, then launch PS2 Library.
3. Wait for the startup scan, move to a game, and press Cross to check its
   files. The title bar shows the game ID and TXT/LUA status.
4. If game files are missing, either FTP-place `<SERIAL>.txt`/`<SERIAL>.lua`
   and press Triangle, or press Cross to create defaults.
5. Optionally tick option boxes; checked lines are appended after the
   `--config` line so they win over the game file without editing it.
   WIDESCREEN, UPSCALING, PROGRESSIVE (`--gs-progressive=1`), and BILINEAR
   (`--gs-force-bilinear=1`) use wiki-documented backend syntax. SCANLINES
   has no CLI flag and never emits (its box stays N/A).
6. Move to LAUNCH and press Cross. The loader is compared, replaced if
   different (numbered backup kept, shared cards backed up first), the
   jailbreak is requested, and the backend launches. R1 re-verifies the
   active loader without writing. The bottom line always shows the latest
   result.

## Emulator package boundary

The PS4 package contains the selected emulator's native `eboot.bin` and helper
runtime. The five Probe 16 packages intentionally carry a 0-byte
`image/disc01.iso`; the external `master.txt` must supply a readable
`--image="/data/PS2/isos/<name>.iso"` path. External CLI/Lua files can change
emulator options, ISO path, and Lua selection, but they cannot replace the
native binary. Therefore the frontend switches by launching the selected
installed title ID; it does not hot-swap an emulator inside one running PKG.

If the active master does not match, the existing confirmed transactional apply
flow remains available through Cross. Launch remains separated from both apply
and privilege-request confirmation.

## Local verification

The build passes the per-game file machine (166 checks), the
transaction/read-only state machine (24,930 + 29,923 checks, including loader
and card-backup paths), the staged-readable HEN request state machine,
controller and launch-gating tests (548), inherited scanner/profile/ISO tests,
exact external RAC3 artifact validation, ELF layout/import checks (no memmove/
memchr-class imports; gamefiles TU builds with `-fno-builtin-memmove
-fno-builtin-memcpy`), signed-container inspection, and nine synthetic 1080p
layout renders. Neither the build nor packaging script connects to the console.

`build/eboot-console-ready.elf` is the live-ShadowMount replacement artifact.
It retains the linked ELF and its terminal `PATH` comment, then clears only the
version/tail metadata that the active console loader was proven to consume. The
same guarded conversion reproduces the known-working Probe 11 console EBOOT
byte-for-byte. `dist/PPSA99202/eboot.bin` remains the signed app-distribution
artifact. The privilege-race fix was built after the earlier UI versions. It
keeps the known launch path and now waits for delayed UID elevation without
changing save data or emulator package contents.
