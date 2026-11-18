# ra9fael.tio — a serial terminal plugin for WinShun

Opens a serial terminal with [tio](https://github.com/tio/tio) bundled inside the plugin package. **Pick the port in the search box and press Enter to start**; every parameter, remote sharing and the defaults live in the standalone window.

Current version: **v0.2.0**.

## Interactive online demo (no install)

The standalone window has an **interactive web mockup**: the port dropdown named the way Device Manager names its ports, the automatic identity capture on selection, the foldable device-details card — all tryable in a browser:

> **[Open the interactive demo](https://ra9fael.github.io/winshun-tio/)** (GitHub Pages, published from `mockup/` on push)

If Pages is not live yet, a zero-setup preview also works:
`https://htmlpreview.github.io/?https://github.com/ra9fael/winshun-tio/blob/main/mockup/index.html`

Note: this is a **UI mockup** with hard-coded sample devices (COM8/COM9). It opens no serial port and does not represent the running plugin; the installed plugin is the authority on behaviour.

> **Remote sharing is explicit bidirectional control, not a read-only log.** **It is on by default.** The port defaults to TCP `54321` and can be set to `1024-65535` in the plugin settings or the standalone window; tio listens on all interfaces. LAN clients can read serial data and write arbitrary bytes to the device. WT and remote clients share one session; enable only on a trusted network. The window also connects to `127.0.0.1:<port>` as a read-only consumer and shows what this shared session receives (last 128 KB). Search-box launches never enable sharing.

## Window screenshot

![tio standalone window: session status, bidirectional sharing, HEX content and log history](https://github.com/user-attachments/assets/f6ec794f-aa0a-4332-b44d-0002d973581c)

> Captured from the running standalone window. The shared-content card switches between terminal and HEX, configures bytes per row and an ASCII decode column; auto-logging is on by default and existing logs are available from **Logs**.

## Install

- **Plugin library**: WinShun → Settings → Plugins → Library, search "tio"
- **Local package**: download the `.wsplugin` from [Releases](https://github.com/ra9fael/winshun-tio/releases) and install it from the file under "Installed"
- **Manual**: unpack into `%LOCALAPPDATA%\WinShun\plugins\ra9fael.tio\` (`plugin.json` at the folder root) and restart WinShun

The first enable asks for `processes` (start the terminal), `ui` (show the window) and `clipboard:write` (copy shared content, the port or diagnostics); bidirectional remote sharing additionally requests listen permission for TCP `1024-65535`. WinShun asks again when a plugin update adds a permission. Requires WinShun **0.30.0** or later (the settings write-back and `settingsWindow` contracts). **No MSYS2 install is needed**: tio and the DLLs it actually imports ship in the package (8 files, 6.6 MB).

## Using it

### Path A: the search box (no window)

Type `tio` and Enter to enter the command; it answers with a group of rows:

```text
  ⏻  tio COM8      COM8 → /dev/ttyS7
  ⏻  tio COM9      COM9 → /dev/ttyS8
  ⌁  Baud rate     115200
  ⌂  Open standalone window   115200 8n1 · none · Automatic (Windows Terminal)
```

| Row | What Enter does |
|---|---|
| Launch row (one per port) | Starts tio on **that row's own port** once the six checks pass; if that port is running, Enter stops that session |
| Baud rate | The common rates replace the list in place, Esc goes back, the current value is ticked; the window also takes any positive integer, and the row's menu opens the window with Ctrl+Enter |
| Open standalone window | The whole configuration and backend on one row; Enter opens the standalone window (every serial parameter, sessions, remote sharing and the defaults live there) |

There are no group captions: every row says what it does on its own, and a running session's row is prefixed with "Running". The serial parameters and the backend are global, so they are shown once, on the window row; a launch row carries only its own port and the `/dev/ttyS` mapping. There is no control-line option in tio and the default behaviour was never measured — that note lives on the window's Port & parameters card, not in the list.

**Where the configuration lives, split by key:**

- **Port, baud rate and backend preference are WinShun's settings** (the three on the plugin's card). Changing the baud rate in the search box, or the port or backend in the window, writes back to the settings page: the card and the plugin show one value, kept across restarts. These three no longer go into `tio.json`. A backend this machine lacks falls back in the auto order at launch, but the preference itself stays as set.
- **Data bits, parity, stop bits, flow control and remote sharing are the plugin's own**, edited only in the standalone window and stored in `dataDir/tio.json`.

A launch takes what is on screen: it writes the port/baud rate/backend it started with back to the settings page, the rest into tio.json. **Saving changes the defaults for later sessions** — a running session keeps the parameters it started with, and new parameters mean restarting it; a settings-page change never starts, stops or reconfigures a session.

### Path B: the standalone window

Reached from its own "**Open standalone window**" row in the result list (Enter), or the launch row's menu (Ctrl+Enter).

The same groups, plus tabs, a Port & parameters card, device identity, session status and diagnostics. An advanced section can explicitly confirm bidirectional remote serial sharing on TCP `54321`; it warns that all interfaces are exposed, remote clients can write to the device, and slow clients may stall tio. Launch and restart require explicit confirmation when sharing is enabled.

"+" opens a session straight away and leaves the port empty until you pick one in Port & parameters; a port is exclusive only while a session runs, so picking the same one is allowed and Launch explains why it cannot go. Remote sharing applies only to the explicitly confirmed standalone-window session; it is not saved as the default and remains off after restart/reopen.

The window draws from the host's `nativeLook` contract: all four styles (fluent / brand / tactile / liquid), the corners, the UI size and the transparency follow the host, and an appearance change repaints live.

Closing the window while a session runs offers three choices: keep it running in the background, stop it and close, or cancel.

### Terminal backend: the user picks, wt first, conhost as the fallback

| | `wt.exe` | `conhost.exe` |
|---|---|---|
| tio's parent | not us (`wt.exe` hands over and exits) | us |
| How Stop works | find it by tab title token + creation time, then terminate it | owned PID + creation time |
| Ownership | **limited**, spelled out in the window | **owned** |
| Window | Windows Terminal's | goes away with tio |

A machine without Windows Terminal falls back to conhost automatically; a pinned backend this machine lacks is not started — the launch falls back in the auto order, the preference itself stays as set, and the window spells out what it resolved to.

### COMn → /dev/ttyS(n-1)

A settled convention: COM1 → `/dev/ttyS0`, COM10 → `/dev/ttyS9`, COM256 → `/dev/ttyS255`. Parsing the name does not claim the port exists; a missing one makes tio itself report `Could not open tty device`.

## Why there is no DTR/RTS switch

The pinned tio 3.9 has **no** command-line option for DTR/RTS (checked against `tio --help`). So there is no mode to offer here, and none is invented. The levels at open, run, clean stop and abnormal exit have not been measured, so the plugin claims neither "held" nor "safe" for the default and makes no promise that opening the port leaves the control lines free of a transient. Real-hardware waveform measurement remains a delivery gate.

## Docs

| Doc | Content | 中文 |
|---|---|---|
| [Design](plugin-design.en.md) | 11 sections, stable requirement ids, priority and acceptance | [设计](plugin-design.md) |
| [UI specification](UI-SPEC.en.md) | the form, parameters, keyboard, states and lifetime | [UI 规格](UI-SPEC.md) |
| [Test plan](TEST-PLAN.en.md) | L0–L5, dependency closure, real hardware, gates | [测试计划](TEST-PLAN.md) |
| [Third party](THIRD_PARTY.en.md) | per-file source, licence and "why it is needed" | [第三方组件](THIRD_PARTY.md) |
| [Implementation](docs/implementation.en.md) | code structure, both paths, building and checks | [实现笔记](docs/implementation.md) |
| [Validation](docs/validation.en.md) | what was run, what it showed, what has not run | [验证记录](docs/validation.md) |

## Building

Needs MSVC (VS 2022 or 2026), CMake 3.21+, Ninja, and Qt **6.12.0** (here `D:\tools\Qt\6.12.0\msvc2022_64`):

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build\msvc -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=D:/tools/Qt/6.12.0/msvc2022_64
cmake --build build\msvc
```

Output: `tio.exe` (the Qt-free starter) and `TioApp.dll` (Qt and the QML window).

### Open the GUI during development (before any `.wsplugin` is built)

```powershell
# visible window from the isolated build, fresh temp dataDir, no serial session
pwsh tools\remote_share_gui.ps1

# hidden startup check: stays up for a few seconds, fails on QML warnings
pwsh tools\remote_share_gui.ps1 -Check

# keep this run's temporary configuration for inspection
pwsh tools\remote_share_gui.ps1 -KeepData
```

In the window, open Session → Advanced · bidirectional serial sharing and confirm the switch and TCP `54321`; Launch and a shared-session Restart ask for confirmation again. The wrapper only opens the window: it does not start tio, overwrite the repository-root binaries, or touch the installed WinShun's configuration.

```bat
build\msvc\tests\tio_tests.exe          :: 292 pure-core assertions (no Qt, no Windows)
build\msvc\tests\tio_native_probe.exe   :: 30 real-machine checks (it does start processes)
python tests\integration.py             :: 52 protocol round trips (--launch adds 7 more real start/stop checks)
python tools\package.py --dev           :: build the .wsplugin and print its SHA-256
```

## Licence

The plugin itself is MIT. The bundled tio and MSYS2 runtime are GPL-2.0-or-later; per-file sources and obligations are in [THIRD_PARTY.en.md](THIRD_PARTY.en.md).
