# PS2 Library - release setup (folders to create)

The app **does not create folders** (it deliberately carries no `mkdir`
import). Every required folder below must already exist, or the matching
feature fails:

- missing `/data/PS2/configs/` -> default `<SERIAL>.txt` / `<SERIAL>.lua` cannot be written
- missing `/data/PS2/covers/`  -> cover downloads fail with `FILE CREATE FAILED`

## App install

```text
/data/homebrew/PPSA99202-app/            (eboot.bin, sce_module/, sce_sys/, assets/)
/user/app/PPSA99202/mount.lnk           -> /data/homebrew/PPSA99202-app
```

## Create these folders once

```text
/data/PS2/isos/          put your *.iso files here (scanned by the menu)
/data/PS2/configs/       per-game <SERIAL>.txt + <SERIAL>.lua live here
/data/PS2/covers/        cover-art cache (auto-filled by the built-in downloader)
```

Example (over FTP; `MKD` is idempotent enough - an existing folder just errors):

```text
MKD /data/PS2
MKD /data/PS2/isos
MKD /data/PS2/configs
MKD /data/PS2/covers
```

## Saves

Saves are held by the **backend package itself** (the emulator's own save
container for that title ID); they are not written to `/data/PS2/saves`.
`/data/PS2/saves/<SERIAL>/` is therefore **not required** and can be ignored.
The per-game `<SERIAL>.txt` still carries a `--path-vmc` line, but this
emulator build does not store its card there.

## First use of a game

1. Drop the ISO in `/data/PS2/isos/`.
2. Rescan with **Triangle**, highlight the game, press **X** to read its serial.
3. Press **X** again to create default `<SERIAL>.txt` / `<SERIAL>.lua` if they
   are missing, then launch.

Several ISOs can share the same backend package, but note that saves belong to
the backend package, not to the individual disc.
