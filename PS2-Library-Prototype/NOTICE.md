# Provenance

The native project is based on BlackBearReloaded's
[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate/tree/722f2227a8bb6fa2229120546995b6562552c752),
commit `722f2227a8bb6fa2229120546995b6562552c752`, GPL-3.0-or-later.
Its license, original notices, independently authored runtime-shim source, and
tooling are retained under `native/`. The temporary BlackBear icon is an
upstream asset; it is not original PS2 Library branding.

Local changes add the profile menu, controller input, bounded ISO metadata
reader, backup/replace transaction, host tests, and database tools; adapt the
renderer; select a new native title ID; and accommodate the workspace path and
installed WSL linker. Our source additions use GPL-3.0-or-later. The menu's
output folder excludes upstream music/backgrounds, but includes its icon.

`native/src/ps5_pad.hpp` is the unmodified public interoperability header from
[ps5-native-gamepad-input-research](https://github.com/blackbearreloaded/ps5-native-gamepad-input-research/tree/16e9b953b26a7102bc801a380f08fbf00060d84b),
commit `16e9b953b26a7102bc801a380f08fbf00060d84b`. Its upstream notice and license
are retained as `native/docs/gamepad-NOTICE.md` and `gamepad-LICENSE`.

The ordinary title-launch ABI was checked against John Tornblom's
[ps5-payload-dev/websrv, src/ps5/sys.c](https://github.com/ps5-payload-dev/websrv/blob/master/src/ps5/sys.c)
(GPL-3.0-or-later). Only the public launch-context layout and function declaration
were used. This menu does not copy that application's process-killing or
privileged launch implementation.

See `native/NOTICE.md` for SDK, LLVM, zlib, and runtime-shim details.

The PSDevWiki snapshot and derived cache retain their source authorship and
licensing separately. See `database/README.md` and the embedded permanent
revision links. Existing user CLI/Lua choices remain user-provided material;
their successful test, not an inferred wiki match, is the RAC3 baseline.
