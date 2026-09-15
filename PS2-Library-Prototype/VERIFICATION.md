# Local verification — prototype 01.000.001

## Latest local build — sixth backend (Rewind) + menu entry (both shipped)

`emu-pkgs/backends-zero-iso/PS2-Rewind-empty-LIBR12352.pkg` (37,421,056 bytes,
SHA-256 `0ADC438AA9B03785365E7961D3A1AF17F7D8521B79AC44ECD990FEAB5CDDF2FA`)
adds the PS2HD-generation core as `REWIND HD` / `LIBR12352` alongside the five
classics; the frontend registry, 488 controller checks, and all container
gates pass with the six-entry menu. The menu eboot
(`dist-vmc-etahen-gamefiles/PPSA99202/eboot.bin`, 648,681 bytes, SHA-256
`E77CA545DCEF1463EC3AAF2842DF7045B286CE536DF16581EA07BF9D38CF204B`)
carries the sixth entry. Both uploaded and hash-verified on the console
(`/data/pkg/`, `PPSA99202-app`).

**Install: CONSOLE-PROVEN (rev 4).** Rev 1-3 were rejected with `0x80120003`;
the live kernel log showed `scePlayGoCoreGetRawContentInfo` failing to parse
the package's PlayGo "content info" (`sce::Json::Parser::parse` = `0x80848101`).
The GoW eboot is a PS2-Classics SELF, so its package needs the source dump's
`Sc0/` scenario bundle; rev 4 stages `Sc0/*` as top-level package files (the
Fake PKG Tools documented step). Rev 4 installs.

**Boot: crashes.** The backend launches (`Big App started pid=364`) and the
PlayGo precheck passes (`CheckIniChunksInstall 0x00000000`), but the core then
crashes (`[Syscore App] App Crash : PID=0x16c, reason=0x4`,
`SCE_SHELL_UTIL_ERROR_APPLICATION_CRASH`). The config chain is correct
(`master.txt` -> `SLUS-20064.txt` -> `/data/PS2/iso/Oni (USA).iso`), and the
WotM/Jak2 cores boot external ISOs with the same chain, so the failure is
specific to the GoW core's boot script. See `emu-pkgs/README.md` (external-image
boot unproven; minimal boot script; AOT absent).

## Previous local build — clean menu + serial-keyed per-game files

`diagnostics/native-launch-probe-12-fixed/dist-vmc-etahen-gamefiles/PPSA99202/eboot.bin`
(648,489 bytes, SHA-256 `80B0464D5FE0A75012EEACD2D3F202BA466762B0AAD9923E936E8CF8781554CB`)
replaces the settings/options page and the catalog-profile flow with a clean
menu: game list, Up/Down game, Left/Right backend, X launch, R1 read-only
verify. Each launch is driven by `/data/PS2/configs/<SERIAL>.txt` +
`<SERIAL>.lua` (missing files prompt to FTP-place or Cross-create defaults;
existing files are never modified). `master.txt` becomes a two-line loader;
the transactional backup/verify and shared-VMC card backup are unchanged.

166 gamefiles checks, 548 rewritten controller checks, 24,930 + 29,923
transaction checks (incl. loader paths), and all container gates pass with no
new imports (gamefiles TU uses `-fno-builtin-memmove -fno-builtin-memcpy`;
the import gate lost probe-11 in the disk cleanup and now diffs against
in-tree `build/blessed-llvm-pie.elf`, with the layout gate re-anchored from
`profile_check` to `game_files`). Not yet console-tested.

## Previous local build — LIBR backend IDs, BACKEND wording, re-anchored import gate

`diagnostics/native-launch-probe-12-fixed/dist-vmc-etahen-libr/PPSA99202/eboot.bin`
(638,633 bytes, SHA-256
`D7BC99E053C27F1CE457FEF892A1E0D11C2526CEB1BEA118253F751AD985465B`)
renames the compiled backend registry `TEST12346`–`TEST12351` to
`LIBR12346`–`LIBR12351` with `BACKEND` status rows, matching the rebuilt
final PKGs. All 29,763 + 24,853 host checks and container gates pass.

Honest note: the Sep-12 disk cleanup deleted `native-launch-probe-11`, which
`verify_build.py` used as its import-delta reference. The gate now falls back
to in-tree `build/blessed-llvm-pie.elf` (captured from the reviewed LIBR tree;
46 UND imports eyeballed: standard libc/syscalls plus the two known launch
imports, six standard NEEDED libs, no `mkdir`/`mount`/process additions).
`native-menu-probe-02` renderer and section checks still run unmodified.
Reinstall all six backends under the new IDs; shared-VMC saves are keyed by
disc serial and survive the move.

## Previous local build — shared-VMC card backup + etaHEN request + settings identity rows

`diagnostics/native-launch-probe-12-fixed/dist-vmc-etahen-cardbackup/PPSA99202/eboot.bin`
(638,633 bytes, SHA-256 `4564f2aaaf599ebfc89c0d534c7262b30d48bb56ea66a2c9af09deab23eb8eb2`)
combines three local changes: privilege requests publish to
`/download0/etahen_jailbreak` (consumed by etaHEN and OnionHEN), the settings
page shows fixed `GAME ID`/`PROFILE` and `VMC`/`LUA` rows, and a shared-VMC
apply first copies each existing `VMC0/VMC1.card` to a never-overwritten
`VMC?.card.backup-NNNN` file before replacing the master. Absent cards are
skipped and read-only checks never back up.

29,763 shared-VMC host checks (including exact multi-chunk card copies with
partial reads/writes, both slots, name-collision skip, oversize abort with
partial cleanup, and read-only no-mutation), 24,853 default-build checks, and
all ELF layout/import/SELF gates passed with no new imports. Not yet uploaded
or console-tested; the WotM `LIBR12347` private-card migration via Lua and the
two-PKG shared-save proof remain the pending console steps.

## Latest result — Probe 07 identifies RAC3 as SCUS-97353

User photo `20260903_203036_00755048.jpg` confirms `GAME ID READ FROM
SYSTEM.CNF`, GAME ID `SCUS-97353`, ISO OPEN descriptor 20, final READ 60 bytes,
CLOSE 0, and eight operations. The working 64 KiB directory scan also still
lists RAC3.ISO and reaches EOF in two reads. This proves bounded selected-ISO
metadata identification on the user's console; the filename was not trusted.

The fresh post-checkpoint launch is pid 101 at about 17:30:21 UTC with successful
`/data` mounting and native EXEC, matching the photo's 20:30:36 local timestamp.
The receive-only capture was stopped with 233,068 bytes saved. See
`diagnostics/native-serial-probe-07/CONSOLE-TEST-20260903.md` for exact evidence.

An offline exact-ID lookup returns the user's `local-console-tested` RAC3 record:
WotM v1 plus the proven external rac3.txt/rac3.lua paths. The wiki exact CLI block
also names War of the Monsters; its unlabeled Lua block remains review-only and
was not paired or activated. No cache, console file, ISO, config, or payload was
changed while interpreting this result.

Probe 07 is now the working ISO listing + game-ID baseline. Native in-app profile
lookup, live CLI/Lua validation, arbitrary-game backend selection, config changes,
and backend handoff/launch remain unimplemented or untested. The result does not
authorize another build, upload, config write, or launch.

## Current deployment — Probe 07 uploaded and console-tested

The user separately approved backing up Probe 06 and replacing only
`/data/homebrew/PPSA99202-app/eboot.bin` with Probe 07. Upload completed at
17:27:47 UTC on 2026-09-03. Source and mounted raw SELF readbacks both match
`11ed69a519850e2e64f455bf43600748ba31c7f165159d33cd98171f7e0b7875`.
The backup matches confirmed Probe 06; mount link is unchanged and no staging
file remains. Evidence: `diagnostics/upload-serial-probe07-20260903T172747305Z/`.
No other final console file was changed, and the assistant did not launch it.

The existing klogsrv was initially no longer listening, but the user restarted
it. A fresh 600-second receive-only capture began at 17:29:09 UTC in
`diagnostics/klog-probe07-20260903T172909649Z/`; the pre-test checkpoint is
17:29:25 UTC, 104,957 bytes / 1,040 nonempty lines. Initial backlog predates the
new launch. The user supplied the successful result recorded above, after which
the capture stopped locally. Separate approval is required for every later
upload or rollback.

## Latest local build — Probe 07 selected-ISO game ID

After the user asked to continue from the successful filename scan, Probe 07 was
built in `diagnostics/native-serial-probe-07/`. It preserves Probe 06's confirmed
64 KiB directory scanner. Cross now performs a bounded, read-only, one-I/O-call-
per-frame walk of the selected ISO's ISO9660 metadata and root `SYSTEM.CNF`, then
shows the normalized `BOOT2` game ID. Config/database access, writes, emulator
selection, and game launching remain absent.

12,574 integrated sanitizer checks, 2,962 dedicated identifier checks, 87
inherited controller checks, eight converter regressions, final alignment/import/
mapping checks, FSELF inspection, five-file ZIP comparison, and five synthetic
actual-Canvas layout reviews passed. The dedicated cases include `SCUS-97353`,
another region, fragmented reads, later descriptors/root sectors, corrupt data,
I/O errors, and the operation cap. Renderer and non-eboot files are unchanged.

Probe 07 eboot SHA-256:
`11ed69a519850e2e64f455bf43600748ba31c7f165159d33cd98171f7e0b7875`.
ZIP SHA-256:
`a725c77afa69beeb22f5151477b6fbea21d62f62a50ea8e1a4d63c9d1156769f`.

This was initially a local result. The later, separately approved eboot-only
upload is recorded above. No ISO, config, payload, mount, or save changed, and
nothing was launched. See the Probe 07 README for exact limits and test steps.

## Latest result — Probe 06 filename scan succeeds on the PS5

User photo `20260903_165111_00375376.jpg` confirms PROBE 06, RAC3.ISO listed,
LISTED 1, SELECTED 1, UNKNOWN TYPE SKIPPED 0, READS 2/4, and SCAN COMPLETE.
OPEN is descriptor 20, final READ is 0 (EOF), and CLOSE is 0. This establishes
successful enumeration/parsing through EOF for the current folder. Uppercase
display is presentation only. The first read's exact byte count is not shown.

The fresh post-checkpoint launch is pid 123 at about 13:50:44 UTC, followed by
native EXEC in the Probe 06 capture. That aligns with the photo's 16:51:11 local
timestamp. The photo proves scan success; the kernel log correlates the launch.
See `diagnostics/native-iso-probe-06/CONSOLE-TEST-20260903.md` for exact evidence.

Probe 06 is now the working filename-list baseline. No rebuild, upload, or
console-file change was performed while recording the result. The receive-only
capture was stopped locally with 177,139 bytes saved. Multi-page libraries,
ISO serial reads, configuration matching, emulator selection, and game launch
remain untested in this frontend. The later user request to continue authorized
the local Probe 07 build above, but not an upload.

## Current deployment — Probe 06 uploaded and console-tested

The user separately approved backing up Probe 05 and replacing only
`/data/homebrew/PPSA99202-app/eboot.bin` with Probe 06. Upload completed at
13:49:16 UTC on 2026-09-03. Both source and mounted raw SELF readbacks match
`6d9187ace2f4ac444af20da2273bb783dd303f5a7901c6c9665e5d8b1e283689`.
The backup matches Probe 05's hash; mount link is unchanged and no staging file
remains. Transaction evidence and backup:
`diagnostics/upload-iso-probe06-20260903T134915926Z/`.

No other final console file was changed and the assistant did not launch the app.
Separate approval remains required for any further upload or rollback.
A 600-second receive-only klogsrv capture started about 13:49:22 UTC in
`diagnostics/klog-probe06-20260903T134922457Z/`. The user subsequently supplied
the successful result recorded above. Initial captured backlog is not this test.

## Latest local build — Probe 06 filename scanner

After the user approved building the scanner with the tested buffer, an isolated
copy was built under `diagnostics/native-iso-probe-06/`. It requests 64 KiB into
a 16 KiB-aligned buffer and caps reads at four, preserving the previous 256 KiB
requested-byte budget. The filename parser and native directory API adapter are
unchanged. A READS counter identifies progress; any nonzero close result now
prevents a retry, matching the newer diagnostic probes' defensive handling.

12,573 sanitizer-backed host assertions, 87 inherited controller checks, eight
ELF regressions, target alignment/mapping/import checks, FSELF integrity checks,
and ZIP content hashes passed. Three host-only synthetic screens were rendered
with the unchanged Canvas and visually inspected. Prior Probe 03/05 executables
remain unchanged. Filename parsing and EOF were subsequently confirmed by the
user's console test recorded above.

New eboot SHA-256:
`6d9187ace2f4ac444af20da2273bb783dd303f5a7901c6c9665e5d8b1e283689`.
The local app and ZIP are ready; see the Probe 06 README for paths and limits.
No console operations occurred during that local build. The later, separately
approved eboot-only upload and successful console test are recorded above.

The previous receive-only Probe 05 capture ended at 13:41:40 UTC on 2026-09-03,
at its time limit, with 268,733 bytes. It is no longer an active capture.

## Previous result — 64 KiB directory reads succeed; 4/16 KiB fail

The user supplied both Probe 05 pages (`20260903_163347_00458417.jpg` and
`20260903_163351_00789476.jpg`). For both paths, both API styles, and both flag
sets, 4 KiB and 16 KiB reads fail with EINVAL, while 64 KiB and 256 KiB reads
return 0x10000 (65,536 bytes). All opens and closes succeed. No guard/length
stop occurs. The matched fresh launch is pid 120 at about 13:33:16 UTC.

This identifies the scanner's 4 KiB read buffer as too small for the observed
directory reads. 64 KiB is the smallest tested successful size, not a proven
universal minimum; alignment was fixed at 16 KiB throughout. At that point,
filename parsing, EOF iteration, and ISO/config access were untested. Probe 06
has since confirmed parsing and EOF, but not ISO/config access. Earlier OPEN/ENOENT
is a separate, still unexplained historical issue. See the Probe 05 console-test
note for the complete matrix and evidence.

The follow-up read-only scanner was built as Probe 06 and subsequently uploaded
after separate approval, as recorded above. No console files changed during
interpretation or the local build. Ask before any further upload.

## Previous deployment — Probe 05 uploaded and console-tested

The user explicitly approved uploading Probe 05 and backing up Probe 03.
The replacement completed at 13:31:27 UTC on 2026-09-03. Source and mounted raw
SELF readbacks both match the expected Probe 05 hash. The previous executable
matches Probe 03 and is backed up under
`diagnostics/upload-read-probe05-20260903T133126917Z/`, along with transaction
evidence. Mount link is unchanged, and no staging file remains. Only the final
eboot changed; no other console files were modified and the assistant did not
launch the app. Ask before every further upload or rollback.

A fresh 600-second receive-only klogsrv capture started around 13:31:40 UTC in
`diagnostics/klog-probe05-20260903T133139965Z/`. The user completed the Triangle
test and supplied photos of both pages. Completion of the matrix
does not imply every read succeeded; examine the raw READ/ERRNO results.

After the user agreed to the next directory-read diagnostic, Probe 05 was built
under `diagnostics/native-read-probe-05/`. It compares 32 fixed cases: native
and POSIX APIs, `/app0` and `/data/PS2/isos`, plain and directory/no-follow opens,
and 4/16/64/256 KiB read sizes. Every successful open gets exactly one read and
a close; buffer guards, invalid lengths, and close failures stop further work.
It reads directory entries only, without parsing them or opening ISO/config
files. No filesystem writes or backend launches.

101,023 sanitizer-backed host assertions, 87 previous controller checks, eight
ELF regressions, layout/import/BSS checks, and FSELF/ZIP checks passed. Both UI
pages were rendered and visually inspected with synthetic data using the actual
Canvas. Renderer bytes and non-eboot app files are unchanged; NEEDED libraries
are unchanged. The new POSIX directory API is `getdents`; no new helper library
was introduced. It now has the mixed hardware results recorded above; it is not
yet a working filename scanner.

Executable SHA-256:
`6d38bfb93064a943fef24b032528598f615ab46b62b220905607ec6920a04396`.
See its README for exact limits, hashes, and two-photo test instructions. Old
Probe 03 and Probe 04 artifacts remain unchanged. No console operations were
performed during the local build; the later approved upload is recorded above.
The prior retest
klog capture ended at 13:18:45 UTC (217,823 bytes, time limit).

## Previous result — Probe 03 opens the folder, but enumeration fails

The controlled retest photo `20260903_161015_00715063.jpg` shows DIRECTORY READ
FAILED: OPEN 0x00000014, READ 0x80020016 (EINVAL), CLOSE 0. The scan failed;
opening and closing alone succeeded. No filenames were successfully listed.
This is a different failing stage from the earlier OPEN/ENOENT. Do not describe
the scanner as fixed or infer an empty ISO folder from LISTED 0.

Capture `diagnostics/klog-probe03-20260903T130845433Z/kernel.raw.log` records a
fresh pid 117 launch at about 13:10:04 UTC, matching the photo's 16:10:15 local
filename timestamp, with successful `/data` mounting. Later launches of pids
118 and 119 are also logged, but their on-screen results have not been supplied.

Source and disassembly show the read forwards the opened descriptor, an owned
buffer, and length 4096 to `sceKernelGetdents`. The error branch is taken before
directory record parsing for that call. EINVAL does not identify which argument
or filesystem condition was rejected. Buffer/read API requirements remain to
be tested; the screenshot itself did not authorize a new build or upload.
The subsequent user agreement authorized the local Probe 05 build recorded
above. The subsequent, separate Probe 05 upload approval was fulfilled as
recorded at the top of this document.

## Previous deployment — unchanged Probe 03 controlled retest

After Probe 04 passed all path opens, the user explicitly approved replacing
only the eboot with the existing read-only ISO-list Probe 03 and backing up
Probe 04. Upload completed at 13:08:33 UTC on 2026-09-03. Both source and mounted
raw SELF readbacks match the unchanged Probe 03 SHA-256
`b96d53bbbc0bd797c5dc2382e7cfbf3168f18de3d6530842743b377d57517e7e`.
The backed-up executable matches the successful Probe 04 SHA-256. Mount link
is unchanged; no staging file remains. Transaction evidence and backup:
`diagnostics/upload-iso-probe03-retest-20260903T130833546Z/`.

No rebuild occurred; no other final console files, payload settings, configs,
ISOs, or saves changed. The assistant did not launch the app. The controlled
retest result is recorded above. Ask before every further upload or rollback.

The Probe 04 capture ended by local stop request at 13:08:33 UTC, with 130,593
bytes. A separate bounded receive-only capture for this retest started at
13:08:45 UTC under `diagnostics/klog-probe03-20260903T130845433Z/`, with a
600-second limit. Correlate a fresh launch; the initial bytes may be backlog.

## Latest console result — path comparison (Probe 04) passed

The user supplied `20260903_160508_00248476.jpg`: TEST COMPLETE, all 16 OPEN
results 0x14 (descriptor 20), all CLOSE results 0, and all POSIX errno values 0.
Native application opens now work at `/app0`, `/data`, `/data/PS2`, and
`/data/PS2/isos`, with both API styles and both flag combinations. The matched
fresh launch is pid 116 at approximately 13:04:46 UTC, with a successful `/data`
mount in the log. This establishes directory opening, not directory enumeration
or ISO/config reads. Probe 03's earlier failure is still unexplained; the same
native path/API/flags succeed in Probe 04. See the Probe 04 console-test note.

The proposed unchanged Probe 03 comparison was subsequently approved and
uploaded as recorded above. No console files were changed while interpreting
the successful Probe 04 result itself.

The user approved building Probe 04 locally and subsequently approved uploading
only its eboot with a backup of the previous version. Upload completed and was
verified at 13:02 UTC on 2026-09-03. Source and mounted
raw SELF hashes match, the mount link is unchanged, and no staging file remains.
Evidence and the previous Probe 03 backup are under
`diagnostics/upload-path-probe04-20260903T130208875Z/`. No other final console
files changed, and the assistant did not launch the app. Ask before further
uploads or rollbacks.

Probe 04 is under `diagnostics/native-path-probe-04/`. Triangle compares
read-only native and POSIX opens, with and without directory/no-follow flags,
at `/app0`, `/data`, `/data/PS2`, and `/data/PS2/isos`. It shows raw returns and
separate POSIX errno values, closes successful descriptors, and stops on any
close failure. It does not enumerate directories, read ISO/config content,
write files, or launch another app. It is diagnostic, not a confirmed fix.

43,588 sanitizer-backed host assertions, the 87 prior controller checks, eight
ELF regressions, layout/import gates, and FSELF/ZIP integrity checks passed.
Renderer bytes and all non-eboot app files are unchanged from the proven native
baseline; NEEDED modules are unchanged. New imports relative to Probe 02 are
exactly `sceKernelOpen`, `sceKernelClose`, and `__error`. Old Probe 03 and working
alignment-fixed Probe 02 hashes remain unchanged. Probe 04 executable SHA-256:
`0378cfe786d2d13f6f5b8c60b95e770b463aa536ee9cc72f1485eb4ded1024a8`.
Hardware directory-open behavior is now confirmed as recorded above. See its
README for artifacts, scope, and test instructions. A bounded receive-only
klogsrv capture started at 13:02:15
UTC under `diagnostics/klog-probe04-20260903T130215984Z/` (600-second limit).

## Current failing access test — read-only ISO list (Probe 03)

The user approved building the next diagnostic locally. It is isolated under
`diagnostics/native-iso-probe-03/`, with Triangle-triggered enumeration of
`/data/PS2/isos`, paginated filenames, and on-screen raw I/O results. It does not
open ISOs or configs, write files, or launch another app. Native `/data` access
remains unproven until the user tests this build. The user subsequently approved
the Probe 03 eboot upload; it completed at 12:25 UTC on 2026-09-03. Source and
mounted-path raw SELF readbacks both match the expected hash, the mount link is
unchanged, and the previously working eboot is backed up under
`diagnostics/upload-iso-probe03-20260903T122500565Z/`. Only the final `eboot.bin`
changed; the assistant did not launch the app. Ask before any further upload.

The working alignment-fixed Probe 02 and its console deployment are preserved.
Probe 03 passed 12,282 sanitizer-backed host assertions, the 87 existing
controller checks, eight prior ELF regressions, new binary layout/import gates,
and FSELF/ZIP integrity checks. Renderer machine code and all non-eboot app files
are unchanged; NEEDED libraries are unchanged. New imports are the three
read-only directory APIs and `memcpy`/`strlen`. Executable SHA-256:
`b96d53bbbc0bd797c5dc2382e7cfbf3168f18de3d6530842743b377d57517e7e`.
Hardware result: Probe 03 launches and Triangle reports OPEN `0x80020002`
(ENOENT), with READ/CLOSE not called. The fresh log shows the first launch before
the patch payload started, and later launches with successful `/data` mounting.
FTP now lists `rac3.iso` inside the app sandbox as well as the original folder.
The user reports the same ENOENT even after restarting; the mount-timing
explanation is insufficient. Latest launch pid 115 also has a successful mount
record. Binary inspection confirms the exact lowercase `/data/PS2/isos` path and
`sceKernelOpen` flags 0x20100. Next proposed diagnostic is per-path/API/flag
read-only comparisons, not another blind restart or folder change. Probe 04 is
now built, uploaded, and console-tested successfully as recorded above.
See `diagnostics/native-iso-probe-03/CONSOLE-TEST-20260903.md` for evidence and
timing. No console changes were made during this diagnosis.

## Current console-tested native menu baseline

`diagnostics/native-menu-probe-02-aligned/dist/PPSA99202/eboot.bin`
is now user-confirmed working ("yes its good", following the menu/input test
request). It was uploaded with explicit permission and verified at the source
and mounted paths. Fresh klog confirms native execution as pid 100; ShadowMount
records PPSA99202 started at 14:52:02, app_id 0x00004018. The previous RELRO
alignment error is absent from this controlled launch. Preserve this artifact
and its SHA-256 `6155c36725c624015c9e1e5840b9cab7c118b8873a7303caf800916325ef975f`.

Kernel evidence: `diagnostics/klog-probe02-20260903T115125876Z/kernel.raw.log`.
This establishes the native menu baseline, not native `/data` reads/writes,
ISO scanning, database activation, or launching the PS4 backend. The original
full menu has not yet been rebuilt/deployed with the corrected converter.
Continue with external-file access as a separate read-only test. Ask before
every console upload.

## Confirmed native loader defect and fix — 2026-09-03

Fresh klogsrv capture of the user's Probe 02 launch identified a non-page-aligned
ELF segment #2: alignment 0x4000, VA 0x8000, file offset 0xC0A0. The converter
used the GOT's location as the RELRO segment's beginning; the correct offset is
0xC000. The same defect exists in the original full-menu output. This is a
concrete loader rejection before app code, not evidence against the controller
API or the PS2 emulator.

The converter source is corrected, and the **exact same Probe 02 intermediate
ELF** was re-converted into a separate app under
`diagnostics/native-menu-probe-02-aligned/`. Eight layout regression tests and
the 87 existing controller checks passed. Re-converting the working Hello World
produced byte-identical output. Old tools/artifacts remain preserved; no console
upload was performed. The corrected app still needs a console retest.

Corrected eboot SHA-256:
`6155c36725c624015c9e1e5840b9cab7c118b8873a7303caf800916325ef975f`

Raw launch evidence:
`diagnostics/klog-probe02-20260903T113432967Z/kernel.raw.log`, lines 1259–1261.

After explicit user approval, the alignment-fixed eboot was uploaded at
11:50 UTC on 2026-09-03. Both the source and mounted-path readbacks match its
SHA-256; the old eboot is backed up under
`diagnostics/upload-probe02-aligned-20260903T115053646Z/`. No other final console
files were changed and no launch was performed. Console runtime retest remains
pending. Ask the user before every subsequent upload, including any rollback.

## Console follow-up — 2026-09-03

- The original full menu failed to start with **CE-107750-0**.
- The user replaced only its executable with `diagnostics/native-boot-probe-01`
  and supplied a successful **HELLO WORLD / APP0 ASSET LOADED** screenshot.
- The live ShadowMount log subsequently recorded `PPSA99202` started, pid 104,
  app_id `0x0000E018` at 01:57:32. Runtime, metadata, assets, and app identity
  were unchanged for that test.
- Native template startup, basic display, and packaged-asset reading are now
  demonstrated on this PS5 Pro 11.40 setup. The cause of the full menu failure
  remains undetermined. Controller input, native `/data` access, and app handoff
  are **not** established by the Hello World result.
- The boot-only executable and original failed menu are preserved separately.
- `diagnostics/native-menu-probe-02` is the next isolated display/controller
  test: 87 host-mocked checks and native build/container/import checks passed.
  The user reported CE-105773-3, but a read-only FTP follow-up found the correct
  Probe 02 eboot present in the source folder while the mounted app directory
  was empty and `mount.lnk` absent. The log records a missing source executable,
  two failed mount attempts, exhausted retries, and later ShadowMount shutdown.
  Resolve this deployment blocker before attributing failure to the binary.
  Details and the downloaded executable/log are in
  `diagnostics/probe-02-upload-check/`. No console writes were performed.
- Later, the remount was confirmed both in the 14:26:44 log entries and direct
  FTP checks of the mounted files/link. The user still reported launch failure.
  The missing mount is no longer the active blocker; the native startup cause
  remains unresolved. Next: correlate fresh klogsrv output with one Probe 02
  launch. Kernel backlog alone is not proof of a current failure.

## Passed

- Native Clang/LLD 18 build completed; both eboot and independently authored
  runtime containers passed the foundation's integrity checks.
- 47 host profile/ISO checks passed after the final source edits. These include
  correct serial and expected image size, wrong-region rejection, unchanged
  master during validation/failure, exact backup bytes, complete replacement,
  exclusive backup names, stale-lock handling, symlink rejection, missing-data
  failure, and a pre-launch check against the active master.
- 14 offline database tests passed: source/revision checks, complete pinned
  snapshot, normalized exact IDs, actual region-specific emulator notes,
  no inherited serial labels, local override priority, missing/unverified
  backend rejection, duplicate local profile rejection, non-overwriting output,
  and ISO identity independent of the filename.
- Native metadata reader identified the actual local RAC3 source ISO as
  `SCUS-97353`. The separate database ISO reader returned the same serial and
  selected the recorded local `wotm-v1` override. Neither hashed/copied the ISO.
- Native controller header's upstream notice and GPL license were retained.

## Artifact fingerprints

SHA-256 values, computed from the final files:

```text
a6bd28f4a2f331ffe60526d2991931f74f73bb8c9756f153b79ac58b3ea0dcf6  native/dist/PPSA99202/eboot.bin
e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036  native/dist/PPSA99202/sce_module/libc.prx
b465e6fa5efd6d49e0638d592c9cf8fa9a565833cc4b827b19b3945c64ecfe89  upload/data/PS2/library/profiles/rac3.txt
```

The native eboot is 32,409 bytes; its runtime shim is 1,284,674 bytes.
`package-test.ps1` refuses to replace an existing ZIP and verifies every archived
file byte-for-byte against the built folder.

## Working baseline preserved

The original `tutorial-launcher.pkg` remains unchanged:

```text
9aa73cfcc748d5d5d7c7f21a7a38bbce36568d47686af7d4df0f7805491b1648
```

The prepared RAC3 profile has the same digest as the previously successful
`rac3-external-test/upload/data/PS2/configs/master.txt`. The existing RAC3 Lua
copy and the original Desktop `lua.lua` both remain:

```text
a6269c0f0c10f986d1e89a4f86f8c0cf6ab1a3495bd991ea021700896b13c972
```

No console writes, uploads, app launches, payload changes, or save modifications
were performed by the local build. No new PS4 backend PKG was produced.

## Not verified

- Native menu rendering/controller ABI on this PS5 Pro 11.40 setup.
- Native app access to the existing `/data` mount, including write/rename access.
- Native-menu-to-PS4-app automatic handoff. The normal request may fail, or
  opening another game may close/suspend the menu; this needs an isolated test.
- Emulator compatibility beyond the user's demonstrated WotM v1 tests.
- Automatic large-library scanning, database activation, per-game memory cards,
  and additional emulator backend packages: not implemented in this prototype.
- No guarantee of power-loss durability, ISO content authenticity, or executable
  revision identity from serial/size checks alone. Do not replace the tested
  source image with a modified or different revision under the same filename.

Do not use apply or handoff until native menu startup is resolved. The original
README's first-test workflow is deferred while isolated diagnostics are tested.
