# PS2 Library — first native menu test

This adds a separate native PS5 menu. It does **not** replace or rebuild your
working `tutorial-launcher.pkg` (`TEST12345`).

> Current build: `diagnostics/native-launch-probe-12-fixed/` (see its README
> for the workflow). Launching is now per-game and serial-keyed:
> `/data/PS2/configs/<SERIAL>.txt` + `<SERIAL>.lua`, created as working
> defaults when absent, with `master.txt` replaced by a two-line loader. The
> profile/catalog flow and the settings/options page described below are
> retired; the sections after this notice are kept as history.

Status: compiled and container-integrity checked locally; 47 native-profile/ISO
checks and 24 offline database/converter tests passed. **Not yet tested on your
console.**
The immediate test is whether the menu renders, reads the controller, and can
read your already-working `/data/PS2` files. Automatic app handoff is a separate,
unverified experiment.

The current UI rebuild is available under
`diagnostics/native-launch-probe-12-fixed/dist-ui9/`. It preserves
the proven launch path and additionally accepts external
`status=as-is` profiles (`lua=-`, optional `size=0`). The established
`diagnostics/native-launch-probe-12-fixed/dist/PPSA99202/` output is unchanged.
The UI rebuild opens on the settings page by default. D-pad Up/Down moves
through backend and option rows, and X selects/toggles. Selecting a backend
keeps focus on its row. “Select ISO from Library” opens the ISO list; pressing
X on an ISO returns to the same settings page with a larger centered “Launch
Selected ISO” action. Circle stays on this page. Long ISO names wrap safely.
Widescreen and upscaling are now applied transactionally to `master.txt`
(`--host-display-mode=16:9`, `--gs-uprender=2x2`, and
`--gs-upscale=edgesmooth`); scanlines, deinterlace, and bilinear remain
preview-only until their backend syntax is verified. Startup, scanning, apply,
and privilege waits use a segmented 8-bit green loading bar, and the old
startup debug toast is suppressed.

The default launcher preserves the emulator's native `--path-vmc="/tmp/vmc"`
setting. The separate `diagnostics/native-launch-probe-12-fixed/dist-vmc-bridge/`
build enables automatic cross-emulator persistence by rewriting the active CLI
to `--path-vmc="/data/PS2/saves/<disc-serial>"`. WotM, JAK2, and other backend
packages then address the same raw PS2 VMC for a game because the directory is
keyed by the disc serial, not the backend package title ID. Create each serial
directory once before first launch. Existing encrypted PS5 native save
containers are left untouched; this build does not copy or decrypt `sdimg_*`
or `.bin` files.

The PNACH converter can stage a serial-addressed native profile tree under
`database/native-stage-lotr/`. Copy only its `data/PS2/` contents to the
corresponding existing `/data/PS2/` tree after reviewing the generated profile;
the supplied SLES-52020 candidate is intentionally marked `review-required`.

## First test — read-only

1. Fully close the existing PS2 app, not just suspend it. Keep your working
   onionHEN/kstuff/data-mount setup. Do not change the working launcher, master,
   Lua, ISO, or save files.
2. Check that title ID `PPSA99202` is unused on your console. If it is already
   used, stop; do not replace that app.
3. Extract `PPSA99202.zip` and upload the **whole** `PPSA99202` folder using your
   native-app/ShadowMount+ workflow. The foundation's directory deployment path
   is `/data/homebrew/PPSA99202/`. It must contain `eboot.bin`, `sce_module/`,
   `sce_sys/`, and `assets/`. Do not upload the ZIP itself or just `eboot.bin`.
   If your loader does not support directory apps, report that before changing
   the working PS4 PKG.
4. Upload just these two **new** profile files, creating their parent folders
   under the existing `/data/PS2` if needed:

   ```text
   upload/data/PS2/library/profiles/rac3.txt
       -> /data/PS2/library/profiles/rac3.txt
   upload/data/PS2/library/profiles/ulaunchelf.txt
       -> /data/PS2/library/profiles/ulaunchelf.txt
   ```

   Do not upload the whole project. Neither upload replaces `master.txt`.
   If these profile files already exist from another project, preserve them
   first rather than overwrite them.
5. Let ShadowMount+ register the new native app, then open **PS2 Library
   Prototype**. Use Up/Down to select **ULAUNCHELF BASELINE**, then press
   **Triangle**. Stop here for the first test. Opening, browsing, and Triangle
   validation do not modify `master.txt`.

Expected: two selectable entries and `PROFILE AND REQUIRED FILES CHECKED`.
Please report whether the menu appears, whether the controller responds, and
the exact Triangle result. The temporary icon is the upstream BlackBear icon.

If `/data` is inaccessible to the native app, expect `CANNOT READ CURRENT MASTER
OR DATA MOUNT`. Do not create a replacement master or another `/data` tree to
hide that error. The native app's mount access has not yet been demonstrated,
even though the PS4 emulator's access has.

## Second test — profile switching, only after Triangle works

The two profiles target the **same user-tested WotM v1 backend** for this first
test. That is not the policy for other games.

- Cross checks the selected profile and asks you to confirm that the PS2 app
  is fully closed. A second Cross creates a backup and replaces the complete
  `/data/PS2/configs/master.txt` with the selected profile.
- Circle cancels a pending confirmation. Returning to the menu after an apply
  does not undo that apply.
- After applying, close this native menu using the PS5 interface and manually
  open your existing PS2 Tutorial Launcher. This tests profile switching without
  depending on automatic handoff.
- RAC3 requires `/data/PS2/isos/rac3.iso` (4,379,377,664 bytes; actual disc serial
  `SCUS-97353`) and your existing `/data/PS2/configs/rac3.lua`. Its prepared TXT
  is byte-identical to the previous successful external RAC3 test. No ISO needs
  to be copied again.
- uLaunchELF uses the ISO already embedded in the launcher and clears external
  Lua with `--config-local-lua=""`. It has no RAC3 hooks.

Do not FTP-edit the master/profile files while the menu is applying a profile.
Changes still require a full close/relaunch; this is not live game patching.
RAC3 performance is unchanged. The VMC behavior is experimental until a save
has been created, closed, and reopened through two emulator packages on-console.

## Optional third test — automatic handoff

After a successful apply, R1 opens a separate launch confirmation. Cross
rechecks the active master and files, then sends **one** ordinary
`sceSystemServiceLaunchApp` request for the selected backend's title ID.

This caller context may not be allowed to start another game while this menu
is active. A nonnegative result means only that the request returned without
an error, not that RAC3 booted. A negative result is displayed in hexadecimal.
If the call does not return, close the native menu from the PS5 interface.
In either case the manual close-menu/open-PS2-app path remains the fallback.

There is no process killing, credential elevation, ShellUI patching, payload
injection, mount creation, or automatic retry in the menu.

## Recovery and diagnostics

- Before replacement, the previous master is backed up byte-for-byte as
  `/data/PS2/configs/master.txt.library-backup-0001`, then `0002`, etc. Older
  backups are never overwritten. Backup and staged bytes are read back before
  replacement; the resulting master is read back afterward.
- To restore, close both apps and preserve the current master under another
  name, then copy the desired backup to `master.txt` over FTP. Do not move or
  delete your only backup.
- The menu writes a temporary `.library-write.lock` in the configs directory
  only during apply. If a crash leaves it behind, close both apps and inspect
  the master/backups. Remove **only that stale lock** once no write is running.
  A leftover `.pending` file is not an active config and can be retained for
  diagnosis. The menu does not remove old backups or pending files.
- Files are flushed and replacement uses same-directory rename. This is not a
  tested guarantee against power loss, filesystem damage, or concurrent edits
  by software that ignores the lock.
- Best-effort title-local logging uses `/download0/ps2-library.log` inside the
  native app's sandbox. Its FTP-visible backing location is not established;
  the on-screen message/error code is sufficient for the first test.
- Never accept an unexpected memory-card format prompt as part of this test.

## Correct emulator and database selection

The intended selection is **disc serial/region/revision + compatible emulator
build + its matching CLI/Lua**, with your tested local overrides taking priority.
There is no universal "best" base assumed by this project.

The current native prototype has serial-addressed profiles plus a compiled-in
backend registry. It can launch the console-tested WotM v1 (`TEST12345`) or
JAK2 v2 (`LIBR12348`) package selected in the default settings page. It scans
the external ISO directory and applies the two verified display options before
launch; it does not yet download/apply wiki entries automatically.

The offline database groundwork is in `database/`: 206 game sections and 314
code blocks from the user-selected PS4 PSDevWiki list, revision 297152. It can
identify a local ISO through `SYSTEM.CNF` and return exact-serial candidates,
preserving source labels and emulator notes. Ambiguous/untested snippets are
review-only and never executed. No silent WotM fallback is present.

The separate `database/pnach_to_wotmv1.py` host tool now indexes the added
PCSX2 PNACH collection, matches files by the ISO's exact serial/CRC, and emits
reviewable WotM-v1 Lua plus CLI candidates. It does not change this native
menu's compiled profile list or upload anything to the PS5. Use the generated
Lua/CLI only after checking the report and testing that serial/revision with
the selected backend; PCSX2 patches are not automatically interchangeable with
WotM v1. If no exact PNACH exists, `convert --allow-no-match` emits an as-is CLI
fragment without `--config-local-lua`, so the ISO remains launchable without a
silently substituted patch.

For other emulator builds the planned backend is a separate small launcher PKG
with a distinct title ID and matching runtime/support files. We have **not**
built, installed, or console-tested those backends. A wiki family name without
a version does not establish which maker build is correct. Loading the larger
catalog into the native menu follows verification of the native read/write and
handoff boundaries above. See `database/README.md` for the implemented tool and
its limits.

## Build and local verification

Source: `native/src/`. Complete app folder: `native/dist/PPSA99202/`.
Archives are transport files only. The checked-in foundation docs describe its
generic demo; use this README for this prototype's behavior.

The build uses Ubuntu WSL, Clang/LLD 18, public PS5 payload SDK v0.42, zlib 1.3.2,
and the foundation's independently authored runtime shim. WSL compiler/build
dependencies were installed with your approval. No Sony SDK was installed.
Generated dependencies remain under `native/.deps/native/`.

From Ubuntu/WSL, with this directory as the current directory:

```sh
bash native/tools/build.sh Folder
bash tests/run.sh '/mnt/c/path/to/your/RAC3.iso'
python3 tests/test_catalog.py
```

The optional ISO test reads bounded filesystem metadata, not the whole image.
Host profile-test fixtures are created under `/tmp/ps2-library-tests.*` and
preserved for diagnosis. Their large fake ISOs are sparse files; do not copy
those fixture images to the console. Previous app-folder builds are retained
under `native/dist/previous/`.

See `NOTICE.md`, `native/LICENSE`, and `VERIFICATION.md` for provenance and build
checks. No PS2 ISO, game data, emulator executable, or Sony runtime is included
in the native-menu ZIP.
