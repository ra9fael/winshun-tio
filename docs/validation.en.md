# Validation record

[中文](validation.md) · [Index](../README.md) · [Implementation](implementation.en.md)

What was run on which machine, what it showed, and what has not run. **Only what this file calls "measured" has been verified.**

Environment: Windows 11 x64 ｜ MSVC 19.51 (VS 18 Community) ｜ CMake 3.29.2 ｜ Ninja 1.12 ｜ Qt 6.12.0 (`D:\tools\Qt\6.12.0\msvc2022_64`) ｜ WinShun 0.30.0 (`C:\Program Files\WinShun`) ｜ serial devices on this desk: `Silicon Labs CP210x (COM8)` and `WCH CH347 dual serial (COM10, COM11)`

## 1. The checks that ran

The 0.2.1 round (the port list following the desk, one session no longer blocking the others) is measured below; the 0.2.0 and 0.1.4 rounds are kept in sections 2 and 3.

| Layer | Command | Result | Notes |
|---|---|---|---|
| L0 | `build\msvc\tests\tio_tests.exe` | **299 / 299 pass** | new: a running session blocks its own port only (COM8 running leaves COM9's row a selectable `launchPort`), a shared gate (`Package`) still reaching every row, and a port missing from the enumeration having no row |
| L0' | `build\msvc\tests\tio_window_tests.exe` | **124 / 124 pass** | the window model (unchanged this round): the custom baud rate accepted/refused, the auto preference surviving a save, key-scoped saves, the dirty form surviving an outside change, backend availability, diagnostics |
| ctest suite | `ctest --test-dir build\msvc -C Release` | **5 / 5 pass** | tio_logic, tio_window_model, tio_share_client, tio_ansi_text, tio_share_port_preflight |
| L2 | `python tests\integration.py` | **55 / 55 pass** | new: the answer carries `rerun`, asking again answers the same rows, and the rows' port set equals what `tio.exe --probe ports` enumerates at that moment |
| L2+ | `python tests\integration.py --launch` | **67 / 67 pass** | two real sessions on two devices at once: COM8 running leaves COM10's row launchable, COM10 starts while COM8 runs, both rows say Stop, stopping one leaves the other running; then Stop → Launch on the first |
| L2' | `python tests\window_check.py` | **28 / 28 pass** | zh/en × light/dark + the host's four styles, no QML warnings |
| Packaging | `python tools\package.py --dev` | pass | `dist\development\ra9fael.tio-0.2.1.wsplugin`, sha256 `4c8b0aac84c6c184c45c541b9d205c37d7d69c8eaa49f2f054531e67c41bc41e` |
| Install | `ws plugin install` | pass | 0.2.0 → 0.2.1, enabled |
| Device plug and unplug | observation script, three windows totalling 25 minutes | **not measured** | the port set stayed `COM8, COM10, COM11` throughout (a later process started on `COM8` alone and never changed). What the change rests on: every answer re-reads the ports, `samePorts` decides whether anything moved, and `testPortIsolation` asserts that a port missing from the enumeration has no row. The physical unplug is left to do with the device at hand |

The 0.2.0 round (the configuration-ownership rework):

| Layer | Command | Result | Notes |
|---|---|---|---|
| L0 | `build\msvc\tests\tio_tests.exe` | **292 / 292 pass** | pure C++; the main list's shape (launch rows + baud + open-window, no port/control/diagnostics rows), the backend preference resolution, `FormEdits` key sets, `sameForm`, the appearance contract |
| L0' | `build\msvc\tests\tio_window_tests.exe` | **125 / 125 pass** | the window model: the custom baud rate accepted/refused, the auto preference surviving a save, key-scoped saves, the dirty form surviving an outside change, backend availability, diagnostics |
| ctest suite | `ctest --test-dir build\msvc -C Release` | **5 / 5 pass** | tio_logic, tio_window_model, tio_share_client, tio_ansi_text, tio_share_port_preflight |
| L2 | `python tests\integration.py` | **52 / 52 pass** | protocol round trips, the `winshun/settings/update` handover, a host value overriding a local one, migration filling only what the page lacks, the main list's shape, the window's entry rows |
| Packaging | `python tools\package.py --dev` | pass | the digest is in the `.sha256` beside it under `dist\development\`; the runtime verification is part of the packaging flow |
| Install | `ws plugin install` | pass | 0.1.4 → 0.2.0, enabled |

The 0.1.4 round (hardware and post-packaging items not re-run this round):

| Layer | Command | Result | Notes |
|---|---|---|---|
| L1 | `build\msvc\tests\tio_native_probe.exe` | **30 / 30 pass** | starts real processes; both backends measured |
| L2+ | `python tests\integration.py --launch` | **76 / 76 pass** | adds one real session: the launch row, the running label, Enter to stop; COM8 opens again right after, no orphan process |
| L2' | `python tests\window_check.py` | **28 / 28 pass** | zh/en × light/dark (4) + the host's four styles (fluent/brand/tactile/liquid, each with a full nativeLook) + square corners, no QML warnings |
| L3 | `python tests\host_smoke.py` | **16 / 16 pass** | started the way the host starts it (`WS_QT_DIR` + JSON-RPC), through the real DLL |

## 2. Measured facts

| Check | Result |
|---|---|
| Package integrity | all 8 runtime files match; flipping one byte of `msys-lua-5.5.dll` makes it report a hash mismatch at once (measured in the previous round; the logic is unchanged) |
| `tio --version` | `tio 3.9` |
| `tio --help` | **no** DTR/RTS option (this is why the plugin offers no control-line switch) |
| Real port | COM8 → `/dev/ttyS7`; tio prints `Connected to /dev/ttyS7` |
| Missing port | `/dev/ttyS9` → `Could not open tty device (No such device or address)`, exit code 1 |
| No console | without `CREATE_NEW_CONSOLE` tio prints `Non-interactive mode enabled` and exits at the first disconnect → the conhost path **must** pass the flag (measured) |
| wt ownership | after `wt -w new --title …` tio's parent is the resident `WindowsTerminal.exe`, not `wt.exe`; it is found by parent + creation time + a unique match, and `TerminateProcess` removes tio while the terminal window stays |
| Backend probe | `wt=1 version=1.25.2733.0 conhost=1` (the version comes from `HKCU\…\AppModel\Repository\Packages`, without starting a terminal) |
| Relative-path defect | running L1 with a relative plugin folder failed the wt path (wt hands the tio path to a command line Windows Terminal resolves against its own working directory) → `Controller::configure` now forces an absolute path, and the same test then passed |
| Group row ids changed with the language | the caption is localized but the id was derived from it, so after a language switch the host could not find the row it was on → ids are fixed (`group:launch` / `parameters` / `information`); L0 and L2 each assert it |
| The launched configuration was not written | `launch()` adopted the form without saving it, so "A is running while the next query shows B" → it saves before starting, and L2+ checks `port` / `packageVersion` in `tio.json` |
| A running row looked idle | after a launch the row was indistinguishable from the idle one → running / starting / stopping is now in the subtitle |
| Host settings were ignored | `settingsChanged` used to discard its payload (0.1.x: seeded only keys `tio.json` had never recorded) → **since 0.2.0 the page owns port/baudrate/backend**: its record is the user's latest answer, a pushed value is adopted at once and never sent back; the file's old values are handed over once, and the page is the authority afterwards (covered by L2, including a numeric field sent as text and a page change overriding the value in force) |
| A save pinned `auto` to `wt` | the form stored only the resolved backend, so saving any other field turned `auto` into `wt` → the preference (`BackendPreference`) and the resolution are stored and published apart; auto stays auto across saves and restarts (covered by L0') |
| The page's three keys landed in `tio.json` | the public keys used to be written to both the file and the settings page, so the two could diverge → `writeSettingsLocked` now removes them from the file, which keeps only the advanced keys and the UI state (L2 asserts the file lacks all three) |
| A theme or language switch did not repaint the window | `DesignTokens.dark` was a read-only signed property and `appearanceChanged` did not notify the view → it now reads the model's `dark` and calls `Controller::touch()`, without re-probing the port |
| Window rendering | `--window` under the software renderer, captured: the five groups, the controls, both languages and both themes all correct (the capture is in the session record, not in the repo) |
| The icon | the glyph design follows the [winshun-plugin-icons](../../winshun-plugin-icons) pipeline: the Phosphor `wave-square` (U+EA9E) rasterised per size; `app.ico`'s 8 frames and `icons/app-*.png` share pixels; the title bar and taskbar read the `:/plugin-icons` resources through `setPluginWindowIcon()` (the L2' no-QML-warning runs cover that path); font loading moved from `prepareGui` to `startGui` (after the `QGuiApplication`), fixing a crash that made the font code dead on arrival |
| No-Qt fallback | with `WS_QT_DIR`/`WS_APP_DIR` cleared, `tio.exe` still serves the protocol; `openWindow` answers "the window needs the Qt build" and nothing crashes |
| 各契约版本 | `plugin.json` 声明 `"qt": "host"`, `"qtVersion": "6.12.0"`；宿主 `Qt6Core.dll` 实测 6.12.0.0 |
| `winshun/settings/update` | after a baud-rate choice in the search box the plugin emits `winshun/settings/update` (`baudrate` as a number); the host records it and answers `settingsChanged`; the plugin does not re-send the same value (no echo loop) |

## 3. Not run (delivery gates)

None of this has been verified. Do not write it up as passing:

| Item | Why not | Consequence |
|---|---|---|
| The four-stage DTR/RTS waveform on real hardware | needs a scope and a controllable load | **blocks delivery**: the README and the window therefore say "not measured", never "held" or "safe" |
| A human look at the window inside the host | L3 now raises the window through the `openWindow` action (and checks the protocol still answers while it is up), but nobody looked at it | DPI, theme following and a screenshot still need a person |
| Unplug/replug misfire | needs repeated unplugging of COM8 | the identity snapshot and "a new device on the same number never takes over" have logic tests only |
| A genuinely busy port | needs a process holding COM8 | the upstream text `Could not open tty device` is measured, but whether "busy" and "absent" are distinguishable is not |
| Unicode / spaced install path | not installed to one | the wt `;`/`|` refusal has a test and Unicode goes through `wide()`, but it was never really installed there |
| High port numbers (COM10+) | this machine only has COM8 | parsing and mapping are tested; no hardware |
| Clean-machine closure | MSYS2 is installed here | **blocks release**: "the machine happens to have MSYS2" must not hide a missing DLL |
| Performance calibration | not measured | `memoryMB: 128` and `timeoutMs: 1500` are estimates, not measurements |
| Per-component licence notices | only the original texts are included; no notice compilation | **blocks release**: GPL/LGPL obligations need formal text, see [THIRD_PARTY](../THIRD_PARTY.en.md) |
| 200% DPI / multiple monitors | not tested | the window follows Qt's scaling, but there is no screenshot evidence |
| Screen reader | not tested | the QML controls have default `Accessible` roles, but Narrator was never run |

## 4. Known defects, fixed and open

| Defect | State |
|---|---|
| The wt backend failed when the plugin folder was relative | **fixed**: `Controller::configure` calls `GetFullPathNameW` (covered by the L1 regression) |
| The native window read the port box as empty because the `GetWindowTextW` buffer was one character short | gone with the native window |
| A machine without the shared Qt could not start the plugin at all | **fixed**: `tio.exe` degrades to a windowless protocol server, so the search box still works |
| `LNK1104` when `TioApp.dll` is in use | not a code defect; close the preview window before rebuilding — noted in the [implementation notes](implementation.en.md) |

## 5. Reproducing

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build\msvc -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=D:/tools/Qt/6.12.0/msvc2022_64
cmake --build build\msvc

build\msvc\tests\tio_tests.exe
build\msvc\tests\tio_window_tests.exe          :: needs Qt (the DLLs are copied beside it)
build\msvc\tests\tio_native_probe.exe        :: starts real processes; free the serial port first
python tests\integration.py
python tests\window_check.py                  :: needs a Qt 6.12.0 folder
python tests\host_smoke.py                    :: needs an installed WinShun
python tools\package.py --dev
```
