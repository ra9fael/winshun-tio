# Validation record

[中文](validation.md) · [Index](../README.md) · [Implementation](implementation.en.md)

What was run on which machine, what it showed, and what has not run. **Only what this file calls "measured" has been verified.**

Environment: Windows 11 x64 ｜ MSVC 19.51 (VS 18 Community) ｜ CMake 3.29.2 ｜ Ninja 1.12 ｜ Qt 6.12.0 (`D:\tools\Qt\6.12.0\msvc2022_64`) ｜ WinShun 0.22.0 (`C:\Program Files\WinShun`) ｜ this machine's serial port: `Silicon Labs CP210x (COM8)`

## 1. The checks that ran

| Layer | Command | Result | Notes |
|---|---|---|---|
| L0 | `build\msvc\tests\tio_tests.exe` | **280 / 280 pass** | pure C++; the diagnostics view's row split, the window entry rows, and the appearance contract's parsing (style/size allow-list, square corners, the no-nativeLook fallback) included |
| L0' | `build\msvc\tests\tio_window_tests.exe` | **46 / 46 pass** | the window model: option lists, in-place editing, refusal of unsupported values, the dirty form surviving an outside change, backend availability, diagnostics, fold persistence |
| L1 | `build\msvc\tests\tio_native_probe.exe` | **30 / 30 pass** | starts real processes; both backends measured |
| L2 | `python tests\integration.py` | **70 / 70 pass** | protocol round trips, `replace`, the settings write, the host's settings seeding, the diagnostics view's expand/copy, and the window's entry rows |
| L2+ | `python tests\integration.py --launch` | **76 / 76 pass** | adds one real session: the launch row, the running label, the settings write, Enter to stop; COM8 opens again right after, no orphan process |
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
| Host settings were ignored | `settingsChanged` used to discard its payload → it now seeds: only keys `tio.json` has never recorded are taken, the user's own edit wins (covered by L2, including a numeric field sent as text) |
| A theme or language switch did not repaint the window | `DesignTokens.dark` was a read-only signed property and `appearanceChanged` did not notify the view → it now reads the model's `dark` and calls `Controller::touch()`, without re-probing the port |
| Window rendering | `--window` under the software renderer, captured: the five groups, the controls, both languages and both themes all correct (the capture is in the session record, not in the repo) |
| The icon | the glyph design follows the [winshun-plugin-icons](../../winshun-plugin-icons) pipeline: the Phosphor `wave-square` (U+EA9E) rasterised per size; `app.ico`'s 8 frames and `icons/app-*.png` share pixels; the title bar and taskbar read the `:/plugin-icons` resources through `setPluginWindowIcon()` (the L2' no-QML-warning runs cover that path); font loading moved from `prepareGui` to `startGui` (after the `QGuiApplication`), fixing a crash that made the font code dead on arrival |
| No-Qt fallback | with `WS_QT_DIR`/`WS_APP_DIR` cleared, `tio.exe` still serves the protocol; `openWindow` answers "the window needs the Qt build" and nothing crashes |
| Contract versions | `plugin.json` declares `"qt": "host"`, `"qtVersion": "6.12.0"`; the host's `Qt6Core.dll` measured 6.12.0.0 |

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
