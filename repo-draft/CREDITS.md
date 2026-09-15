# Credits & third-party notices

Twiso exists because of a lot of community work. Project-authored code is
**GPL-3.0-or-later**; bundled third-party components keep their own licenses.
Items marked **TODO** need an exact link/author confirmed before publishing.

> **Nothing in this repository bundles emulator runtimes, packaged backends,
> Sony code, or game data.** The emulator/packaging tools below are
> referenced because they were used locally to *produce* packages the user
> builds themselves; they are **not distributed** here. Only project-authored
> code and permissively-licensed assets (Inter font, button icons) are shipped.

## Jailbreak / runtime

- **etaHEN** — App jailbreak/HEN daemon used to grant launch privileges.
  Author: LightningMods. <https://github.com/LightningMods/etaHEN> *(TODO confirm link)*
- **OnionHEN** — alternative all-in-one HEN/Toolbox (also consumes the same
  jailbreak request). *(TODO: repo link/author)*
- **Sandbox-escape / `/data` access patch payload** — grants the app and the
  emulator backends access to `/data`. *(TODO: name + repo link)*
- **klogsrv** — kernel log server used by the capture script.
  *(TODO: repo link/author)*
- **ftpsrv** — FTP server (port 2121) used for deployment/readback.
  [ps5-payload-dev](https://github.com/ps5-payload-dev) — *(TODO confirm)*

## Native app foundation

- **ps5-native-app-boilerplate** — Copyright (C) 2026 BlackBearReloaded,
  GPL-3.0-or-later, commit `722f2227a8bb6fa2229120546995b6562552c752`.
  <https://github.com/blackbearreloaded/ps5-native-app-boilerplate>
- **ps5-native-gamepad-input-research** — `ps5_pad.hpp` interoperability
  header, commit `16e9b953b26a7102bc801a380f08fbf00060d84b`.
  <https://github.com/blackbearreloaded/ps5-native-gamepad-input-research>
- **PS5 payload SDK** — [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk)
  v0.42 (John Tornblom and contributors). Libc++ headers retain
  Apache-2.0 WITH LLVM-exception. *(TODO: SDK license field)*
- **websrv `src/ps5/sys.c`** — reference for the title-launch ABI context.
  John Tornblom, GPL-3.0-or-later.
  <https://github.com/ps5-payload-dev/websrv>
- **http2_get sample** — reference for `sceNet`/`sceSsl`/`sceHttp2` usage.
  [ps5-payload-dev/sdk samples](https://github.com/ps5-payload-dev/sdk)

## Emulator backends & packaging (referenced only — **not distributed**)

- **Ps2-Classics-emulators-for-Ps4** — the emulator runtimes used as backend
  bases (WotM v1/v2, JAK2 v2, RECVX, Red Faction, KOF2000). *(TODO: repo/author)*
- **PS2-FPKG / Exact CLI PS2 Maker** — used to build the backend PKGs.
  *(TODO: repo/author)*
- **LibOrbisPkg** — by maxton, used to package the frontend into a fake PKG.
  <https://github.com/maxton/LibOrbisPkg> *(TODO: license)*
- **DiscUtils** — MIT; ISO/disk helpers bundled with the maker.
  <https://github.com/DiscUtils/DiscUtils>
- **SharpProspero** — public PS5 package-format reference during development
  (not fetched, copied, or required by the build).
- **God of War Betrayal [Port PS2toPS4]** — earlier REWIND research reference.
  *(TODO: author/credit if referenced)*

## Assets

- **ps2-covers** — cover art source used by the app at runtime.
  xlenore, <https://github.com/xlenore/ps2-covers> *(check repo license)*
- **Inter** — UI typeface. Rasmus Andersson, **SIL Open Font License 1.1**.
  <https://github.com/rsms/inter>
- **PS5 Button Icons and Controls** — button-prompt artwork, **CC BY 3.0**.
  Product: "PS5 Button Icons and Controls"; Author: Zacksly;
  Source: <https://zacksly.itch.io>. Icons were **modified** (recolored to white
  and rasterized into a bitmap atlas).
- **BlackBear icon / selection art / `snd0.at9` "Night Drive"** — original
  upstream assets from ps5-native-app-boilerplate, Copyright (C) 2026
  BlackBearReloaded, GPL-3.0-or-later.

## Host / test

- **GoogleTest 1.17.0** — BSD-3-Clause (host tests only; not linked into any
  PS5 artifact). <https://github.com/google/googletest>

---

No proprietary Sony runtime module, encryption key, emulator binary, or game
file is distributed by this repository. If you believe something here is
mis-attributed, please open an issue.
