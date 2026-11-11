# Changelog

## [Unreleased]

### Standalone window overhaul

- **Parameter segmented radios**: data bits, parity, stop bits, and flow control switch to the same segmented radio track as the host's plugin pages (`TokenSegmented`, sunken rail with a moving pill, drawn from the `nativeLook` contract). Baud rate keeps its dropdown because the candidate list is long. Keyboard semantics are unchanged: Tab stops at the first segment, arrows change the value.
- **Card fold fix**: clicking anywhere on the title row toggles fold. The previous hot region was a strip below the title that didn't overlap it, so a click on the title never landed — the fold looked broken.
- **Device identity expands**: the main row now shows the readable identity and capture time only; a "Device details" / "Collapse details" toggle reveals manufacturer, product, VID/PID, serial, bus, driver, location, instance id and Windows-reported problem code. VID/PID come straight from the device's own hardware IDs via SetupAPI — the parent USB hub's IDs are no longer read as if they were the chip's; FTDI's `VID/PID/serial` use the `+` separator. Capture time no longer sits on the main line; it lives in the details.

## [0.1.0] - 2026-11-10

First implemented version.

### Two paths, one state

### Diagnostics expand in the list

Enter on the search box's diagnostics row **expands a diagnostics view**: the report is shown line by line (Back on top, Esc returns), and Shift+Enter on the first row copies the whole report; it no longer copies at once.

- **Search box (path A)**: the `tio` command answers with grouped rows — a launch row, six parameter rows, two information rows. Enter on a parameter applies at once: port / baud rate / backend use `effect::replace` to show a candidate list (Esc goes back, the current value is ticked), while data bits / parity / stop bits / flow cycle in place. A change is written to `dataDir/tio.json` immediately; there is no Save button in the search box.
- **Standalone window (path B)**: `Ctrl+Enter` opens a QML window. The same groups, plus a read-only port dropdown with a refresh, the device identity and when it was captured, a collapsed explanation of the control lines, a session block, the diagnostics text, and Save / Launch / Stop. Closing it while a session runs offers keep-in-background, stop-and-close, or cancel.
- Both paths share the one `Controller` in `controller.h` (`core.h` holds the process-wide instance), so a parameter changed in one face is the parameter the other face sees.

### Launch

- `wt.exe` first, `conhost.exe` when it is absent; the user can pick either in the candidate list or the window, and a choice the machine does not have is refused and marked with the reason in the UI.
- The wt path finds tio by tab-title token plus creation time (`wt.exe` only hands over and exits; tio's parent is the resident `WindowsTerminal.exe`). The conhost path creates the process itself with `CREATE_NEW_CONSOLE`; without that flag tio prints `Non-interactive mode enabled` and exits at the first disconnect.
- The current form is written to disk before the launch, so what runs is what the next `query` shows.

### The window's entry (visible)

- The result list has its own "**Open standalone window**" row (Enter) — no chord to memorize.
- The launch row's menu opens the window too (Ctrl+Enter): no keyboard detour while reading that row.

### The look (as WinShun's)

- The window draws from the host's `nativeLook` contract: the colours and radii of all **four styles** (fluent / brand / tactile / liquid) follow automatically; square corners zero every radius; the UI size (compact / normal / roomy) sets row heights and font sizes; with transparency on, translucent colours are composited over the base before drawing.
- The window repaints live when the host's appearance changes (`appearanceChanged` → `ThemeBridge::reload`), without re-probing the port.
- An old host without `nativeLook` falls back to the built-in neutral table, reported honestly via `ThemeBridge.native`.
- `nativeLook` has no caution/success tokens; those two keep readable fixed values (documented).

### The settings page

`plugin.json` offers seven defaults: port, baud rate, data bits, parity, stop bits, flow control and backend. They **seed** only: a key is taken from the settings page only when `tio.json` has never recorded it, so a change there never quietly swaps the port in use — what the user edited in the search box or the window wins.

### Honest boundaries

- A successful launch means the process started. The plugin does not read tio's terminal output, so the port state stays `unknown` by default and never pretends to be "connected".
- The pinned tio 3.9 has no DTR/RTS command-line option, so there is no control-line switch; the default behaviour has not been measured, so neither "held" nor "safe" is claimed.

### Runtime

- The recursive PE import closure of tio 3.9-3: 8 files, 6.6 MB, with a per-file SHA-256 in `manifest/files.json`, verified before launch.
- Six pre-launch checks: port, parameters, device identity, package integrity, backend, session. Any failure greys the launch row **and names which one**.

### Icon and font

- The icon comes from the shared [winshun-plugin-icons](../winshun-plugin-icons) pipeline: the Phosphor `wave-square` glyph (U+EA9E, a lime tile), rasterised per size from the font outline into `app.ico` (8 frames) and `icons/app-*.png`, one visual language with the host and every ra9fael plugin; Phosphor's MIT licence ships along (`icons/PHOSPHOR-LICENSE`). The manifest's `icon` points at `app.ico`; the Qt window embeds the same PNGs through `setPluginWindowIcon()`.
- The window's font loads Alibaba PuHuiTi from the host's `fonts\` (file path when ASCII, memory when not), **after** the `QGuiApplication` is constructed — font loading needs the platform backend, and an earlier call crashes outright.

### Folding and syncing

- All seven cards fold, and the folded state is remembered per card title in `tio.json`'s `ui.collapsed`, surviving a restart.
- The session state is verified against the process: `refresh()` checks the recorded PID plus creation time (guarding against PID reuse), and an open window polls every 2 s, so a terminal-side exit shows in the GUI within two seconds.

### Tests

- **L0** 280 pure-core assertions (no Qt, no Windows), the diagnostics view's row split, the window's entry rows and the appearance contract's parsing included.
- **L0'** 46 window-model assertions (tio_window_tests): option lists, in-place editing, refusal of unsupported values, the dirty form surviving an outside change, backend availability, diagnostics, fold persistence. It needs Qt, so CMake copies Qt6Core.dll / Qt6Gui.dll beside it.
- **L1** 30 real-machine checks; **L2** 70 / **L2+** 76 protocol round trips (the diagnostics view and the window's entries included); **L2'** 28 window checks (the four styles + square corners); **L3** 16 checks against the real DLL the way the host starts it.

### Fixed

- The plugin folder is made absolute: wt places the tio path in a command line Windows Terminal resolves against its own working directory, so a relative path would start nothing.
- The empty-buffer device identity read (`GetWindowTextW` needs `length+1`).

### Known gaps

The in-host L3 round trip, the four-stage DTR/RTS waveform on real hardware, a clean-machine closure re-check, performance calibration, and the formal per-component licence notice compilation. See [docs/validation.en.md](docs/validation.en.md).
