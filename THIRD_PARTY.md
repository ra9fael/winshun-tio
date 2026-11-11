# 第三方组件

[English](THIRD_PARTY.en.md) · [索引](README.md)

包里除了本插件自己的 `tio.exe`（引导器）和 `TioApp.dll`（窗口），还带一份 **tio 3.9-3** 及它真正导入的 MSYS2 运行时。这里逐文件列出：来源包、SHA-256、许可证原文位置，以及**为什么这个文件必须在包里**。

`manifest/files.json` 是机器可读的那一份，运行时启动前会据此逐个校验；本文件是人读的那一份。

## tio 与运行时

来源：`https://repo.msys2.org/msys/x86_64/tio-3.9-3-x86_64.pkg.tar.zst`
包 SHA-256：`01029a45605add22b81d2fb18a19b7db86f99631b431d909d062b7b3cda4fcac`
架构：x86_64 ｜ 许可证：**GPL-2.0-or-later**

| 文件 | 字节 | SHA-256（前 16 位） | 许可证原文 | 为什么必须在包里 |
|---|---:|---|---|---|
| `runtime/tio.exe` | 168 542 | `a39c99f2d4cbbc4c` | [licenses/tio.txt](licenses/tio.txt) | 就是这个程序 |
| `runtime/msys-2.0.dll` | 3 393 153 | `d5983eccf5ac3340` | [licenses/msys2-runtime-COPYING](licenses/msys2-runtime-COPYING)、[CYGWIN_LICENSE](licenses/msys2-runtime-CYGWIN_LICENSE) | `tio.exe` 的 PE import；MSYS/POSIX 层本身 |
| `runtime/msys-glib-2.0-0.dll` | 1 300 209 | `19ce0ad8b2b98100` | 同上（GLib 为 LGPL-2.1-or-later，随 MSYS2 分发） | `tio.exe` 的 PE import |
| `runtime/msys-iconv-2.dll` | 1 103 690 | `50012969ed96a523` | 同上 | `msys-glib-2.0-0.dll` 的 PE import |
| `runtime/msys-pcre2-8-0.dll` | 443 825 | `709737df8708749e` | 同上 | `msys-glib-2.0-0.dll` 的 PE import |
| `runtime/msys-lua-5.5.dll` | 254 120 | `48aabb6694e56d04` | 同上 | `tio.exe` 的 PE import（脚本配置） |
| `runtime/msys-gcc_s-seh-1.dll` | 144 609 | `38118e2bb6231318` | [licenses/gcc-libs-COPYING3](licenses/gcc-libs-COPYING3)、[COPYING.LIB](licenses/gcc-libs-COPYING.LIB)、[RUNTIME.LIBRARY.EXCEPTION](licenses/gcc-libs-RUNTIME.LIBRARY.EXCEPTION) | `msys-2.0.dll` 的 PE import（异常展开） |
| `runtime/msys-intl-8.dll` | 111 181 | `50f7de0c712d867d` | 同上（libintl 为 LGPL-2.1-or-later） | `msys-glib-2.0-0.dll` 的 PE import |

**合计 6 919 329 字节（6.6 MB）。**

## 在依赖闭包里但没有入包

`pacman` 的依赖闭包里有这几个包，但递归查 PE import 后**没有任何一个包内 DLL 导入它们**，所以不放进来：

| 组件 | 目录里的许可证原文 | 不适用理由 |
|---|---|---|
| `gcc-libs`（除 `msys-gcc_s-seh-1.dll` 外） | [gcc-libs-COPYING3](licenses/gcc-libs-COPYING3) | 只有 `msys-gcc_s-seh-1.dll` 被导入；其余运行库没有 PE import |
| `zlib` | [zlib.txt](licenses/zlib.txt) | 无 PE import（静态链在 `msys-glib-2.0-0.dll` 内） |
| `libffi` | [libffi.txt](licenses/libffi.txt) | 无 PE import（同上） |
| `libinih` | — | 无 PE import（编译进 `tio.exe`） |

这些许可证原文仍随包放一份：它们是所分发二进制的一部分来源链，留着比不留给读者省事。若将来某个 DLL 改成动态加载这些库，这张表必须重算。

## 不随包的组件

| 组件 | 状态 |
|---|---|
| Windows Terminal（`wt.exe`） | **不随包**，只调用机器上已装的。不把「本机存在」当成「可再分发」 |
| conhost（`conhost.exe`） | Windows 自带，不随包 |
| Phosphor 字体（图标字形来源） | **不随包**，只有生成产物（`app.ico`、`icons/app-*.png`）。生成脚本与字体属 [winshun-plugin-icons](../winshun-plugin-icons)（MIT），生成图标的版权归使用者；字体的 MIT 许可文本随包放一份：[icons/PHOSPHOR-LICENSE](icons/PHOSPHOR-LICENSE) |
| Qt 6.12.0（Core/Gui/Qml/Quick/QuickControls2） | **不随包**，用 Win顺 宿主的共享 Qt（`"qt": "host"`）。插件内不得混入另一版 Qt DLL、平台插件或覆盖共享路径的 `qt.conf` |

## 本插件自身

| 文件 | 许可证 |
|---|---|
| `tio.exe`（引导器）、`TioApp.dll`（窗口） | MIT，见 [LICENSE](LICENSE) |
| 静态链接的 MSVC 运行库 | 依 Visual Studio 许可随本插件二进制分发 |

## 源码与再分发义务

- tio 是 GPL-2.0-or-later：上游源码 <https://github.com/tio/tio>，锁定版本 3.9。二进制再分发须能提供对应源码。
- MSYS2 runtime 与 glib 等按 GPL/LGPL：对应源码由 MSYS2 提供（`repo.msys2.org` 的 `msys/` 源码包），LGPL 组件保留可替换条件（DLL 独立存放于 `runtime\`，可原位替换）。
- 本仓库不修改任何上游二进制；`manifest/files.json` 的哈希可以核验这一点。
- **未完成**：逐组件的正式 notice 汇编、GPL 书面要约文本、以及发布时随包的源码获取说明，见 [验证记录](docs/validation.md) 的未验证清单。
