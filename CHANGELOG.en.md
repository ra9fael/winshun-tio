# Changelog

## [0.2.2] - 2026-11-26

### MCP use card

- Adds an **MCP use** card to the standalone window: choose Claude Desktop, Claude Code, Cursor or VS Code, generate the matching JSON configuration and copy it; the card never edits AI client files.

## [0.2.1] - 2026-11-20

### Fixes: the list follows the desk, and one session no longer blocks the others

- **The search box re-reads the ports on every answer** (`Controller::refreshPorts()`: enumeration, sessions that left, the focused port's identity), and a shown list asks the host to ask again every 3 s (`queryResult` → `rerun`). The plugin is resident, so before this the rows kept the ports found when the process started: a device unplugged afterwards stayed in the list, and Enter started tio on a port that was no longer there. The same call is made when the standalone window comes up, and the window's 2 s tick now asks only for the ports instead of re-verifying the whole package.
- **A running session blocks its own port only.** The session gate was treated as one no other port could fix, so with COM8 running every other port's launch row read "launch blocked · session active" and could not start. It now belongs with the port's own gates (`port`, `portBusy`, `identity`) and is named by that launch's toast: COM8 running leaves COM9 free to launch.
- A candidate list (the baud rates) survives the host asking the same query again; it is dropped when the words change, as before.

### WinShun MCP serial I/O

- Adds the `tio-io` MCP command: AI can inspect user-shared session status, read `COMn` data by cursor, and create ASCII or HEX serial send plans.
- Sending is executed by the `sendSerial` action of `tio-io_run`; the session token and socket generation are revalidated before the write, so a stale plan cannot write to a new session.
- MCP only accesses a tio session that the user explicitly started and shared in the standalone window. It never opens a port, starts tio, or enables sharing, and search-box launches are not shared.
- Shared-socket writes use 64 KiB chunks and a 500 ms send timeout, so a disconnected or non-reading peer cannot leave an MCP write blocked forever.

## [0.2.0] - 2026-11-18

### Configuration ownership: the host's settings page and the plugin split the keys

- **Port, baud rate and backend preference move to WinShun's settings page** (`minWinShun` raised to 0.30.0, using the `winshun/settings/update` write-back). Changing the port or baud rate in the search box, or the backend in the window, writes back to the page: the plugin's card and the plugin show one value, kept across restarts. These three no longer go into `tio.json`.
- A settings-page change takes effect at once, even while `tio.json` still holds an older value — migration hands the file's old public keys to the page once, and the page is the authority afterwards.
- **The backend preference is kept apart from the resolved backend**: `auto` stays `auto` instead of being pinned to `wt` by a save; a backend this machine lacks falls back in the auto order at launch, and the preference itself is unchanged. The diagnostics show the resolution and the preference.
- **The advanced keys are the plugin's**: data bits, parity, stop bits, flow control and remote sharing live in `tio.json` alone; keys the older manifest carried are migrated into the file on first load.
- The search box stays lean on purpose: one launch row per port (the subtitle carries only the `/dev/ttyS` mapping), the baud-rate shortcuts, and one "open standalone window" row that doubles as the configuration summary. The port row, the backend list, the control-line note and the diagnostics view left the search box — every parameter lives in the window, the control-line note on the window's Port & parameters card, the report in the window's diagnostics card and in `details()`.
- The window's baud rate takes any positive integer (the dropdown is shortcuts, not a limit); the shared port is constrained to 1024–65535.
- **A save changes defaults only**: a running session keeps the parameters it started with, and a defaults change never starts, stops or reconfigures a session; a clean form follows outside updates, and a dirty form submits only the keys the user edited.
- The manifest declares `settingsWindow: true`: WinShun's settings page opens the standalone window from its "Settings" button; `showSettings()` never touches a serial port.

### Tests

- 292 pure-core assertions (preference resolution, `FormEdits` key sets, `sameForm`, the main list's shape).
- 125 window-model assertions (custom baud rate accepted/refused, auto preference persistence, key-scoped saves).
- 52 protocol round trips: the `winshun/settings/update` handover, a host value overriding a local one, migration filling only what the page lacks.

## [0.1.4] - 2026-11-14

### Fixes (packaging)

- **The development wrapper ships as BOM UTF-8**: `tools/remote_share_gui.ps1` had no BOM, and the plugin library's checker rejects such a package — Windows PowerShell 5.1 reads BOM-less UTF-8 Chinese as mojibake. The content is unchanged; only the BOM is added. The standalone window and `-Check` behave as before.

## [0.1.3] - 2026-11-14

### Shared serial viewer, terminal rendering, and clipboard permissions

- The standalone window has a separate shared-content card with an always-visible scrollbar; wheel events stay in the terminal at its top and bottom.
- ASCII terminal rendering handles ANSI colours, including xterm 256-colour, background, bold and reverse video. HEX rows are configurable (8/16/32 bytes), with an optional printable-byte decode column; the meaningless sliding-window offset column is removed.
- Copy and Save as use the text shown in the current view. Clipboard writes use Win32 immediate Unicode data and verify `SetClipboardData`; copying port and diagnostics use the same path. The manifest now requests `clipboard:write` so WinShun's plugin job permits the operation. First enable/update requires approving this additional permission.
- Auto-logging is enabled by default and its preference persists. The sharing card has a log-history dialog that lists existing logs and opens a log or its folder.
- Fixes CR overwrite, session-restart receive cursors, and log-history live refresh; adds loopback, ANSI, clipboard and wheel regression probes.


### Bidirectional remote serial sharing

- Replaces file-log capture and the plugin-owned read-only mirror with tio's native `-S inet:<port>` in the standalone window's advanced section.
- **On by default.** The port defaults to `54321` and can be set to anything in `1024-65535`, from the plugin settings or the standalone window; a change needs a session restart.
- The window is also a read-only consumer: it connects to `127.0.0.1:<port>` and shows what the shared session receives (last 128 KB, with Clear). It never sends, so it cannot write the serial port.
- Search-box launches never enable sharing, so one Enter cannot open LAN write access. The switch is saved with the configuration and can be turned off in the window.
- Warns that the port listens on all interfaces, LAN clients can write to the serial device, WT and remote clients share one session, and a slow client may stall tio.
- Requests `network:listen` for TCP `1024-65535`, preflights port availability, and detects tio exiting during startup.

## [0.1.2] - 2026-11-12

### Fixes (clone, capture wording, search-box ports)

- **Clone no longer hangs**: the button read `tio.sessions` and then looped over it to find the focused tab — that is a **live model**, and every clone grew its `length`, so the loop never ended and cloned until memory ran out (measured: 18.9 GB, then a crash). The button now calls `cloneFocusedSession()`, which takes the index from one snapshot in C++, so QML never iterates a list that its own body grows. The other, read-only walks of `tio.sessions` are unchanged.
- **The capture state stops misleading**: capture is a launch argument, but a session started *before* capture was switched on still showed "on · records after launch", so it looked like it was recording. There is now a fourth state, "this run is not recorded", which says that stopping and launching again is what starts recording, and the box's placeholder says "no log for this run" too.
- **The search box has one row per serial port**: the main list used to carry a **single** launch row pinned to the configured port; now every enumerated port gets its own `tio COM8` / `tio COM9` row, Enter launches with that row's own port, and a row whose port is already running becomes Stop. The Port sub-list under Parameters stays for changing the default configuration. Port-specific gates (no port, port busy, unknown identity) are reported by the launch attempt; only gates no port can fix (package, parameters, backend) are written on every row and disable it.
- **Window checks no longer pop windows**: `tests/window_check.py` passes `--no-show` by default (the window is built, laid out and bound exactly as before, it just never appears); `--shown` opens a visible window. Nobody working at the machine gets interrupted by a test window. The window also has two test hooks: `tio.exe --window --no-show` (build without showing) and `--auto <plus|clone|close|capture>` (run one action and quit, writing progress to `TIO_TRACE_FILE` when it is set); ordinary launches are unaffected.

## [0.1.1] - 2026-11-12

### Crash fix (new session, start capture)

- **The cause was the host's memory ceiling, not the window's logic**: `plugin.json` asked for `runtime.memoryMB` 128, and WinShun applies it as the plugin process's `JOB_OBJECT_LIMIT_PROCESS_MEMORY`; the Qt Quick window alone commits 120–130 MB, so the process sat right at the ceiling. Opening a tab (a new QML delegate) or starting capture (a text box plus a refresh timer) needed the next allocation, failed, and the process exited with `0xC0000005` (access violation) or `0xC0000409` (fast fail) — recorded in the host log as "崩溃（退出码 3221225477 / 3221226505）" followed by "1 秒后重启".
- **Reproduced**: the same `tio.exe` inside a 128 MB job object died on the 5th "+"; with the ceiling at 512 MB it took 10 clicks and peaked around 133 MB. The standalone window uses no job object, which is why `tio.exe --window` never crashed — only the copy installed in WinShun.
- **Fixed** by raising `runtime.memoryMB` to 512 (the host default, ~4x headroom). The native probe now fails when `plugin.json` asks for less than 256 MB, so the ceiling cannot quietly come back down.
- The plugin description was corrected too: the window can now turn capture on (tio writes its own local log, the plugin only reads it), so "the plugin never reads, renders or logs serial content" no longer held.

### Standalone window overhaul

- **Multi-session (session-oriented)**: the window is now tabbed — each tab carries at most one process, so COM8 and COM9 can run at once. The tab bar shows the COM number, the Device-Manager name and a state dot. **"+" opens a session straight away and a port is not a precondition**: the new tab says "no port" and the port is chosen in the Port & parameters card; a port is exclusive only while a session runs, so another tab may pick the same one (the dropdown still marks it "this port is already running in another session") and Launch refuses with "port busy". Tabs close (stop first while running) and clone (parameters copied, no port); Restart = stop, then launch again with the same form. Launch/Stop act on the focused tab only — other tabs' sessions are untouched; host shutdown still stops everything. Every tab is 52px tall.

### Session content (capture)

- **Capture is a launch argument, not a runtime switch**: tio 3.9 takes `-L --log-file <file> --log-append --log-strip` only at launch and cannot open a log while it runs. "Start capture" is therefore a per-session choice made before launch and saved with the default configuration; flipping it during a run takes effect on the next launch, and the window says so.
- **It reads only its own file**: the log goes to `dataDir\sessions\<token>.log`, and the window reads that file (last 32KB, refreshed every second while capturing). It never opens the port, needs no serial-read permission and uploads nothing; closing the tab deletes the log.
- **Session content lives in the session card**: a dashed "Session content" heading — switch, state (capturing / on · records after launch / not capturing), a monospaced view, Copy all, and Save as text (the Win32 save dialog: the host only guarantees `QtQuick.Controls.Basic` and ships no Qt Quick Dialogs).

### Window details

- **Tabs are all one height**: 52px — a tab that names its port draws a second line and a port-less tab only one, and the two used to differ.
- **Console text is no longer mojibake**: the window's notices were written to stderr as UTF-8 with `fprintf` and a CP936 console showed "鎏晳幙…"; when stderr is a console it now goes through `WriteConsoleW` as UTF-16, and a redirected stderr still gets UTF-8.

### Other window changes

- **The search box follows**: the main row still shows the saved configuration (it turns into "Stop" while a session runs that port); the window's other sessions are listed row by row under "Sessions (Enter stops)", and Enter stops exactly that row — no more stop-everything.
- **Package integrity is verifiable**: the card gains "Third-party notes" and "File manifest" buttons that open the **in-package** `THIRD_PARTY.md` and `manifest/files.json` — per-file sources, licences and hashes; what opens is the copy the launcher verified, not a web page.

### The integrity record moves into the card

- **The third-party record is inside the card**: the package integrity card now explains what the package carries — a summary line gives the tio version, the runtime file count and the size (all measured from `manifest/files.json`, none of it hardcoded), and unfolding lists every component: name, version, licence, a source link that opens the upstream, the licence text that ships in the package (the copy the launcher verified), and why it is in there; components that are not bundled are marked. `THIRD_PARTY.md` and `manifest/files.json` stay at the bottom of the card as the appendix, but they are no longer the only way to read the record.
- **A machine-readable twin**: `manifest/third-party.json` mirrors `THIRD_PARTY.md` row by row (packaging refuses to ship when the two drift, and the native probe checks that every component is named in the document and every licence text it links to is really in the package).
- **Buttons in a card header work again**: the fold area of a card header used to cover the buttons in that header (clicking "Launch" folded the card instead) — the fold's click area now sits under the content, so a button wins.

### Earlier

- **Port dropdown matches Device Manager**: each entry now shows the Windows friendly name (`SPDRP_FRIENDLYNAME`, the line Device Manager lists under Ports (COM & LPT), e.g. `Silicon Labs CP210x USB to UART Bridge (COM8)`) instead of a bare `COMn`; the second line keeps the `/dev/ttySn` mapping and the instance id.
- **Identity captures automatically**: picking a port re-reads the identity at once (previewed into the focused tab, in memory only — a save stays an explicit Save), so no button press is needed and the capture timestamp follows. Launch still revalidates per TIO-010.
- **Device details as a fold card**: the part of the old "Device identity" card that duplicated the dropdown (the big friendly-name line) is gone; it is now a top-level "Device details" card — the header carries the capture time, and unfolding shows manufacturer, product, VID/PID, serial, bus, driver, location, instance id and Windows-reported problem code. It starts folded, and the user's own choice is remembered separately (`ui.touched`), so folding another card never resets it.
- **The DTR/RTS card is removed**: the pinned tio 3.9 has no control-line options, and the card only took space; the window no longer shows it (the search-box information row is unchanged).

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
