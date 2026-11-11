# Implementation notes

[中文](implementation.md) · [Index](../README.md) · [Validation](validation.en.md)

## 1. Code structure

Both paths share one state and one core. The split exists so **the pure logic can be tested on a machine with no Windows and no Qt**.

| File | Layer | Content |
|---|---|---|
| [logic.h](../logic.h) | pure core | COMn parsing and the n−1 mapping, parameter validation, argv quoting, backend choice, launch plan, session state machine, the six gates. **No Windows dependency** |
| [rows.h](../rows.h) | pure core | the row model: `Row`, `Lang`, candidate-row builders, the cycle actions. Depends only on `logic.h` |
| [native.h](../native.h) | Win32 | read-only enumeration (SetupDi + SERIALCOMM), SHA-256 + PE architecture check, backend probe, launch/stop, private PATH |
| [controller.h](../controller.h) | state | `Form` / `Snapshot` / `Controller`: configuration, refresh, save, launch, stop, diagnostics. One mutex and a `Notify` callback |
| [core.h](../core.h) | state | the process-wide `Controller`, plus `makeLang()` (the bilingual string table) |
| [rows_build.h](../rows_build.h) | path A | `buildRows(snapshot, browse, lang)`: snapshot to rows |
| [plugin.h](../plugin.h) | path A | the protocol: `query` / `action` / `details` / `settingsChanged` / `shutdown` |
| [probe.h](../probe.h) | entry | `--probe ports / backends / verify`, the read-only diagnostics |
| [window_hook.h](../window_hook.h) | seam | the hook the protocol calls to open the window; the console build installs none, so the action says the Qt build is needed |
| [qmlapp/stub/main.cpp](../qmlapp/stub/main.cpp) | entry | the real `main` of `tio.exe`: finds WinShun's Qt, loads `TioApp.dll`; `--probe` and `--serve` stay in this process |
| [qmlapp/qmlmain.cpp](../qmlapp/qmlmain.cpp) | path B | the DLL entries: Qt on the GUI thread, the protocol on a worker thread, the window hook installed |
| [qmlapp/TioWindow.h](../qmlapp/TioWindow.h) / [.cpp](../qmlapp/TioWindow.cpp) | path B | the model QML sees: `Q_PROPERTY` to read, `Q_INVOKABLE` to change |
| [qmlapp/qml/](../qmlapp/qml/) | path B | `TioWindow.qml` plus `GroupCard` / `FieldRow` / `DesignTokens` / `TokenComboBox` / `TokenSegmented` / `TokenButton` (`TokenSegmented` draws the host's segmented radio from the appearance contract, for the short option rows) |

**Why `tio.exe` has no Qt**: it follows [ra9fael.qr](../../ra9fael.qr) and [ra9fael.binarylab-qml](../../ra9fael.binarylab-qml), and WinShun's own `winshun.capture`. `plugin.json` declares `"qt": "host", "qtVersion": "6.12.0"`; the host sets `WS_QT_DIR` before `CreateProcess`; `tio.exe` (static CRT) adds it to the DLL search folders and then `LoadLibraryExW(TioApp.dll)`. The package therefore does not duplicate Qt, and cannot mix in another Qt over the shared paths.

**It works without Qt too**: when `WS_QT_DIR` / `WS_APP_DIR` is unset (or holds no `Qt6Core.dll`), `tio.exe` does not exit — it serves the protocol without a window, because the search box never needed Qt. The `openWindow` action then answers "the window needs the Qt build" instead of pretending to open one.

## 2. Path A: one search-box round trip

```
WinShun                          tio.exe (protocol thread)
  query ───────────────────────►  TioPlugin::query
                                    buildRows(snapshot(), Browse::None, lang)
  ◄───────────────────────────── items[]  (launch + 6 parameters + 2 information)
  action "openPorts" ──────────►  m_browse = Ports
  ◄───────────────────────────── {effects:[replace(items[])]}
                                    first row is back; each row is COM n → /dev/ttyS(n-1) plus identity
  action "port" (item.id=port:10)►  form.port = L"COM10"; controller.adopt(form)
  ◄───────────────────────────── replace (the main list, port row now COM10)
```

- `adopt()` writes to disk: one choice in the search box is the final answer.
- The `cycle*` actions also `adopt()` and then `replace`, so the value on the row changes at once.
- Every `query` resets `m_browse`: the user typed again, so a candidate list is no longer the answer.
- `details()` returns the diagnostics text, so `Shift+Enter` can read the same block in the content pane.
- **The diagnostics view**: Enter on the main list's diagnostics row expands into `Browse::Diagnostics` — the report is shown line by line (`buildDiagnosticsRows`, `rows.h`), the first row carries the whole report's `copy` and the `copyDiagnostics` action (Shift+Enter copies), the rest are read-only; Back sits on top and Esc returns as well.

## 3. Path B: the window

```
winshunTioMain(appDir)                    GUI thread
  prepareGui()                             Qt library paths = WinShun's
  installWindowHook(...)  ← must be before serve()
  QGuiApplication app
  std::thread serve ──► runServeLoop()  ──► exactly the same TioPlugin as path A
                                           (serve() returning → app.quit())
  app.exec()
```

The window is a `QQmlApplicationEngine` plus `TioWindow.qml`; the model is `tioplug::TioWindow` (`Q_PROPERTY` to read, `Q_INVOKABLE` to change). Its constructor attaches a `Notify` to the `Controller` and uses `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` to push the repaint back to the GUI thread, because the callback arrives on **any** thread (the protocol thread may be in the middle of a launch).

The `openWindow` action runs on the protocol thread; the hook moves window creation to the GUI thread with `Qt::BlockingQueuedConnection`, so the action can honestly answer whether it opened.

### The window and the search box do not fight

- The window edits its own `m_form` and writes back to the `Controller` only on `save()` / `launch()`.
- While `m_dirty` is set, an outside change (a port picked in the search box) does not overwrite what the user is typing.
- `launch()` saves first and then starts: what is on screen is what starts, and what the search box will show afterwards.

### QML uses only modules the contract guarantees

`QtQuick`, `QtQuick.Controls.Basic`, `QtQuick.Layouts` — what the shared-Qt contract promises. It does **not** use WinShun's private QML modules (`WinShun/Theme.qml` and friends), and not `QtQuick.Window` either: in the WinShun install `qml\QtQuick\Window` is empty, and the `Window` type comes from `QtQuick`.

`DesignTokens.qml` is a singleton (`QT_QML_SINGLETON_TYPE` in CMake) and takes dark mode from the `darkTheme` context property; `qmlmain.cpp` reads `AppsUseLightTheme` from the registry for a standalone run, and follows `appearance` inside the host.

## 4. Backend and process ownership

| | wt | conhost |
|---|---|---|
| `CreateProcessW` target | `wt.exe -w new --title "tio COM<n> <token>" --suppressApplicationTitle -- <runtime>\tio.exe …` | `<runtime>\tio.exe …` |
| Flags | `CREATE_UNICODE_ENVIRONMENT` | `CREATE_UNICODE_ENVIRONMENT \| CREATE_NEW_CONSOLE` |
| Finding tio | `findWtChild()`: walk the process table for a `tio.exe` whose parent is `WindowsTerminal.exe` and whose creation time is ≥ the launch moment — it **must match exactly one** | the PID we were handed |
| Recorded | PID + creation time | same |
| Stopping | `stopOwned()` tries Ctrl-C first, `TerminateProcess` after the timeout | same, but tio is in the console we created |

Three details that cannot be skipped:

1. **The wt path must not pass `CREATE_NEW_CONSOLE`**: the terminal creates the console.
2. **The conhost path must**: measured — without it tio prints `Non-interactive mode enabled`, exits at the first disconnect and never reads the keyboard.
3. **wt splits its command line on `;` and `|`**: when the plugin path holds either, `planLaunch()` returns `PlanError::PackagePath` rather than trying to escape it.

`stopOwned()` compares the creation time every time: PIDs are reused, so the same number is not the same process.

## 5. The six gates

`check(Environment)` in `logic.h` is the only decision point; the UI only displays it:

| Gate | Fails when |
|---|---|
| `Port` | the name does not parse (not COMn, zero, out of range) |
| `Parameters` | baud rate ≤ 0, data bits outside 5–8, stop bits not 1/2 |
| `Identity` | enumeration cannot read that port's InstanceID |
| `Package` | any packaged file's hash or architecture does not match |
| `Backend` | resolution ends at `Backend::None` |
| `Session` | a session is already running |

The order is fixed, so the launch row names the same first thing to fix every time. **Parameter rows are never gated**: a broken package must not lock the form.

## 6. Building

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build\msvc -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=D:/tools/Qt/6.12.0/msvc2022_64
cmake --build build\msvc
```

Two targets land in the repository root: `tio.exe` and `TioApp.dll`. The QML goes through `qt_add_qml_module` (URI `Tio`); `QT_RESOURCE_ALIAS` flattens the four `.qml` files into the module root so the components can import each other by name, and `QT_QMLCACHEGEN_ARGUMENTS` is set to `--only-bytecode` so no C++ is generated and whichever Qt 6 the host ships can run it.

`TioApp.dll` cannot be overwritten while an instance is running — close the preview window before rebuilding, otherwise `LNK1104`.

## 7. How the tests are layered

| Layer | Command | Covers | Needs |
|---|---|---|---|
| L0 | `tio_tests.exe` | 280 checks: parsing, validation, quoting, backend choice, plans, session machine, six gates, the row model, cycle order, the diagnostics view, the window's entries, the appearance contract | nothing (pure C++) |
| L0' | `tio_window_tests.exe` | 45 checks: the window model's option lists, in-place editing, refusal of unsupported values, the dirty-form guard, backend availability, diagnostics | Qt Core/Gui, no window |
| L1 | `tio_native_probe.exe` | 30 checks: package verification, backend probe, enumeration, launch/stop on both backends, creation time, the no-console behaviour | real Windows + the packaged runtime |
| L2 | `tests/integration.py` | 70 checks: `query` / `action` / `details` / candidate lists / the settings write / the host's settings seeding / the diagnostics view / the window's entries / language / refusals / restart | can start `tio.exe` |
| L2+ | `tests/integration.py --launch` | 76 checks: the same plus one real session started and stopped | as above, and it holds the serial port |
| L2' | `tio.exe --window` | 28 checks: zh/en × light/dark + the host's four styles + square corners, no QML warnings | WinShun's Qt |
| L3 | `tests/host_smoke.py` | 16 checks: the protocol through the real DLL, including `openWindow` | an installed WinShun |
| L4 | real port | COM8 → `/dev/ttyS7` connects, unplug/replug, a busy port | hardware |
| L5 | clean machine | the closure re-checked without MSYS2 present | a clean Windows |

`tio_window_tests` needs Qt: CMake copies `Qt6Core.dll` / `Qt6Gui.dll` beside the test executable ([tools/copy_qt_dlls.cmake](../tools/copy_qt_dlls.cmake)), because the plugin itself loads Qt from WinShun's folder while a plain test executable imports it directly.
