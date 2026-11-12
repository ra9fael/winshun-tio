# ra9fael.tio — a serial terminal plugin for WinShun

Opens a serial terminal with [tio](https://github.com/tio/tio) bundled inside the plugin package. **Set the parameters and launch from the search box**, or open a standalone window to configure it.

Current version: **v0.1.2**.

## Interactive online demo (no install)

The standalone window has an **interactive web mockup**: the port dropdown named the way Device Manager names its ports, the automatic identity capture on selection, the foldable device-details card — all tryable in a browser:

> **[Open the interactive demo](https://ra9fael.github.io/winshun-tio/)** (GitHub Pages, published from `mockup/` on push)

If Pages is not live yet, a zero-setup preview also works:
`https://htmlpreview.github.io/?https://github.com/ra9fael/winshun-tio/blob/main/mockup/index.html`

Note: this is a **UI mockup** with hard-coded sample devices (COM8/COM9). It opens no serial port and does not represent the running plugin; the installed plugin is the authority on behaviour.

> **The boundary, stated plainly**: this plugin launches tio. It does **not** read, render, forward or log a single byte of serial data. What the terminal shows and what the keyboard sends belong to tio and its own window. So the most this plugin can say is "the process started" — "the port is open" appears only when tio itself gives a verifiable signal, and the default state is "unknown".

## Install

- **Plugin library**: WinShun → Settings → Plugins → Library, search "tio"
- **Local package**: download the `.wsplugin` from [Releases](https://github.com/ra9fael/winshun-tio/releases) and install it from the file under "Installed"
- **Manual**: unpack into `%LOCALAPPDATA%\WinShun\plugins\ra9fael.tio\` (`plugin.json` at the folder root) and restart WinShun

The first enable asks for `processes` (start the terminal) and `ui` (show the window). Requires WinShun **0.22.0** or later (the shared-Qt contract). **No MSYS2 install is needed**: tio and the DLLs it actually imports ship in the package (8 files, 6.6 MB).

## Using it

### Path A: the search box (no window)

Type `tio` and Enter to enter the command; it answers with a group of rows:

```text
Launch (one row per serial port)
  ⏻  tio COM8
     COM8 → /dev/ttyS7 · 115200 8n1 · none · Windows Terminal
  ⏻  tio COM9
     COM9 → /dev/ttyS8 · 115200 8n1 · none · Windows Terminal
Parameters (Enter edits in place)
  ⌁  Port        COM8
  ⌁  Baud rate   115200
  ⌁  Data bits   8
  ⌁  Parity      none
  ⌁  Stop bits   1
  ⌁  Flow control none
  ⌁  Backend     Windows Terminal
Information
  !  Control lines DTR / RTS   defaults not measured · no such CLI option in tio
  ▤  Diagnostics               expand (Shift+Enter copies)
```

| Row | What Enter does |
|---|---|
| Launch row (one per port) | Starts tio on **that row's own port** once the six checks pass; if that port is running, Enter stops that session |
| Port / baud rate / backend | Replaces the list in place, Esc goes back, the current value is ticked |
| Data bits / parity / stop bits / flow | Cycles to the next value in place |
| Diagnostics | **Expands a diagnostics view**: the report is shown line by line in the list, Back/Esc returns; `Shift+Enter` copies the whole report |

The port is no longer buried among the parameters: every enumerated serial port gets its own launch row, so Enter starts that port directly instead of going through the Port sub-list first. A row carries only its port; baud rate, parity and backend still come from the current parameters.

A change is written to disk at once: the search box has nowhere to put a Save button. Launching saves first too, so what runs is what the next query shows. The plugin.json settings page only supplies **defaults** — a key is taken from it only while 	io.json has never recorded one, so a value edited here wins.

### Path B: the standalone window

Reached from its own "**Open standalone window**" row in the result list (Enter), or the launch row's menu (Ctrl+Enter).

The same groups, plus what only a window can hold: tabs, a Port & parameters card (port dropdown with a refresh, baud / data bits / parity / stop bits / flow), the device identity and when it was captured, the session card (phase / port state / PID / session id, and Session content — with capture on it reads the log tio writes itself, with Copy all and Save as text), the diagnostics text, and **Save + Launch + Stop + Restart + Clone**.

"+" opens a session straight away and leaves the port empty until you pick one in Port & parameters; a port is exclusive only while a session runs, so picking the same one is allowed and Launch explains why it cannot go.

The window draws from the host's `nativeLook` contract: all four styles (fluent / brand / tactile / liquid), the corners, the UI size and the transparency follow the host, and an appearance change repaints live.

Closing the window while a session runs offers three choices: keep it running in the background, stop it and close, or cancel.

### Terminal backend: the user picks, wt first, conhost as the fallback

| | `wt.exe` | `conhost.exe` |
|---|---|---|
| tio's parent | not us (`wt.exe` hands over and exits) | us |
| How Stop works | find it by tab title token + creation time, then terminate it | owned PID + creation time |
| Ownership | **limited**, spelled out in the window | **owned** |
| Window | Windows Terminal's | goes away with tio |

A machine without Windows Terminal falls back to conhost automatically; choosing a backend the machine lacks is refused, and the UI lists it as unavailable with the reason.

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

```bat
build\msvc\tests\tio_tests.exe          :: 280 pure-core assertions (no Qt, no Windows)
build\msvc\tests\tio_native_probe.exe   :: 30 real-machine checks (it does start processes)
python tests\integration.py             :: 70 protocol round trips (--launch adds 6 more real start/stop checks)
python tools\package.py --dev           :: build the .wsplugin and print its SHA-256
```

## Licence

The plugin itself is MIT. The bundled tio and MSYS2 runtime are GPL-2.0-or-later; per-file sources and obligations are in [THIRD_PARTY.en.md](THIRD_PARTY.en.md).
