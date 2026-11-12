# Third-party components

[中文](THIRD_PARTY.md) · [Index](README.md)

Besides the plugin's own `tio.exe` (the starter) and `TioApp.dll` (the window), the package carries **tio 3.9-3** and the MSYS2 runtime it actually imports. Per file: source package, SHA-256, where the licence text lives, and **why the file has to be in the package**.

`manifest/files.json` is the machine-readable copy; the launcher verifies every file against it before starting. This is the human-readable one.

`manifest/third-party.json` is the machine-readable twin of this table: the window's "Package integrity" card renders it row by row (component, version, licence, source, where the licence text lives, why it is in the package), so the record can be read without opening a file. This file and that one must change together.

## tio and its runtime

Source: `https://repo.msys2.org/msys/x86_64/tio-3.9-3-x86_64.pkg.tar.zst`
Package SHA-256: `01029a45605add22b81d2fb18a19b7db86f99631b431d909d062b7b3cda4fcac`
Architecture: x86_64 ｜ Licence: **GPL-2.0-or-later**

| File | Bytes | SHA-256 (first 16) | Licence text | Why it must be here |
|---|---:|---|---|---|
| `runtime/tio.exe` | 168 542 | `a39c99f2d4cbbc4c` | [licenses/tio.txt](licenses/tio.txt) | it is the program |
| `runtime/msys-2.0.dll` | 3 393 153 | `d5983eccf5ac3340` | [licenses/msys2-runtime-COPYING](licenses/msys2-runtime-COPYING), [CYGWIN_LICENSE](licenses/msys2-runtime-CYGWIN_LICENSE) | PE import of `tio.exe`; the MSYS/POSIX layer itself |
| `runtime/msys-glib-2.0-0.dll` | 1 300 209 | `19ce0ad8b2b98100` | as above (GLib is LGPL-2.1-or-later, shipped by MSYS2) | PE import of `tio.exe` |
| `runtime/msys-iconv-2.dll` | 1 103 690 | `50012969ed96a523` | as above | PE import of `msys-glib-2.0-0.dll` |
| `runtime/msys-pcre2-8-0.dll` | 443 825 | `709737df8708749e` | as above | PE import of `msys-glib-2.0-0.dll` |
| `runtime/msys-lua-5.5.dll` | 254 120 | `48aabb6694e56d04` | as above | PE import of `tio.exe` (scripted configuration) |
| `runtime/msys-gcc_s-seh-1.dll` | 144 609 | `38118e2bb6231318` | [licenses/gcc-libs-COPYING3](licenses/gcc-libs-COPYING3), [COPYING.LIB](licenses/gcc-libs-COPYING.LIB), [RUNTIME.LIBRARY.EXCEPTION](licenses/gcc-libs-RUNTIME.LIBRARY.EXCEPTION) | PE import of `msys-2.0.dll` (unwinding) |
| `runtime/msys-intl-8.dll` | 111 181 | `50f7de0c712d867d` | as above (libintl is LGPL-2.1-or-later) | PE import of `msys-glib-2.0-0.dll` |

**Total 6 919 329 bytes (6.6 MB).**

## In the dependency closure, but not packaged

`pacman`'s closure contains these packages, but walking the PE imports recursively shows **no packaged DLL imports them**, so they are not included:

| Component | Licence text in the folder | Why it does not apply |
|---|---|---|
| `gcc-libs` (other than `msys-gcc_s-seh-1.dll`) | [gcc-libs-COPYING3](licenses/gcc-libs-COPYING3) | only `msys-gcc_s-seh-1.dll` is imported; no other runtime library is a PE import |
| `zlib` | [zlib.txt](licenses/zlib.txt) | no PE import (statically linked inside `msys-glib-2.0-0.dll`) |
| `libffi` | [libffi.txt](licenses/libffi.txt) | no PE import (same) |
| `libinih` | — | no PE import (compiled into `tio.exe`) |

Their licence texts still ship: they are part of the provenance chain of the distributed binaries, and leaving them out would only make the reader's job harder. If a DLL ever loads one of these at run time, this table has to be recomputed.

## Not packaged

| Component | State |
|---|---|
| Windows Terminal (`wt.exe`) | **not packaged**, only the one installed on the machine is invoked. "It exists here" is not "it may be redistributed" |
| conhost (`conhost.exe`) | part of Windows, not packaged |
| The Phosphor font (the icons' glyph source) | **not packaged**, only the generated artefacts (`app.ico`, `icons/app-*.png`). The generator and the font live in [winshun-plugin-icons](../winshun-plugin-icons) (MIT); the generated icons belong to the user; a copy of the font's MIT licence ships along: [icons/PHOSPHOR-LICENSE](icons/PHOSPHOR-LICENSE) |
| Qt 6.12.0 (Core/Gui/Qml/Quick/QuickControls2) | **not packaged**: the plugin uses WinShun's shared Qt (`"qt": "host"`). A plugin must not mix in another Qt's DLLs, platform plugins or a `qt.conf` that overrides the shared paths |

## The plugin itself

| File | Licence |
|---|---|
| `tio.exe` (starter), `TioApp.dll` (window) | MIT, see [LICENSE](LICENSE) |
| The statically linked MSVC runtime | distributed with these binaries under the Visual Studio licence |

## Source and redistribution obligations

- tio is GPL-2.0-or-later: upstream source <https://github.com/tio/tio>, pinned at 3.9. Redistributing the binary requires being able to supply the corresponding source.
- The MSYS2 runtime, glib and friends are GPL/LGPL: MSYS2 supplies the corresponding source (`msys/` source packages under `repo.msys2.org`); the LGPL parts keep their relink/replace condition (the DLLs sit separately in `runtime\` and can be replaced in place).
- This repository modifies no upstream binary; the hashes in `manifest/files.json` are how that is checked.
- **Not done yet**: the formal per-component notice compilation, the written GPL offer, and the packaged source-availability note. See the unverified list in [validation](docs/validation.en.md).
