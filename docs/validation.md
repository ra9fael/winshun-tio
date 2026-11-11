# 验证记录

[English](validation.en.md) · [索引](../README.md) · [实现](implementation.md)

本轮在什么机器上、跑了什么、结果如何、什么还没跑。**只有这一节里写了「实测」的，才算验证过。**

环境：Windows 11 x64 ｜ MSVC 19.51（VS 18 Community）｜ CMake 3.29.2 ｜ Ninja 1.12 ｜ Qt 6.12.0（`D:\tools\Qt\6.12.0\msvc2022_64`）｜ Win顺 0.22.0（`C:\Program Files\WinShun`）｜ 本机串口 `Silicon Labs CP210x (COM8)`

## 1. 跑过的检查

| 层 | 命令 | 结果 | 说明 |
|---|---|---|---|
| L0 | `build\msvc\tests\tio_tests.exe` | **280 / 280 通过** | 纯 C++；含诊断视图行拆分、窗口入口行、外观契约解析（风格/尺寸白名单、直角、无 nativeLook 回退） |
| L0' | `build\msvc\tests\tio_window_tests.exe` | **46 / 46 通过** | 窗口模型：取值表、就地编辑、拒绝非法值、脏表单不被外部改动覆盖、后端可选、诊断、折叠持久化 |
| L1 | `build\msvc\tests\tio_native_probe.exe` | **30 / 30 通过** | 会真起进程；两条后端路径都实测 |
| L2 | `python tests\integration.py` | **70 / 70 通过** | 协议往返，含 `replace`、落盘、宿主设置种子、诊断视图展开/复制与窗口入口行 |
| L2+ | `python tests\integration.py --launch` | **76 / 76 通过** | 额外起一个真会话：启动行、运行中提示、落盘、Enter 停止；停后 COM8 立刻可再打开，无孤儿进程 |
| L2' | `python tests\window_check.py` | **28 / 28 通过** | 中英 × 深浅 4 组 + 宿主四种风格（fluent/brand/tactile/liquid 各带全套 nativeLook）+ 直角，无 QML 警告 |
| L3 | `python tests\host_smoke.py` | **16 / 16 通过** | 按宿主的启动方式（`WS_QT_DIR` + JSON-RPC）跑真实 DLL 路径 |

## 2. 实测到的事实

| 检查 | 结果 |
|---|---|
| 包完整性 | 8 个运行时文件全对；改 `msys-lua-5.5.dll` 一个字节后立即报 hash mismatch（上一轮实测，逻辑未改） |
| `tio --version` | `tio 3.9` |
| `tio --help` | **没有** DTR/RTS 相关选项（决定了本插件不提供控制线开关） |
| 真实串口 | COM8 → `/dev/ttyS7`，tio 打印 `Connected to /dev/ttyS7` |
| 不存在的端口 | `/dev/ttyS9` → `Could not open tty device (No such device or address)`，退出码 1 |
| 无控制台 | 不带 `CREATE_NEW_CONSOLE` 时 tio 打印 `Non-interactive mode enabled`，断开后立即退出 → conhost 路径**必须**带该标志（已实测） |
| wt 路径归属 | `wt -w new --title …` 之后 tio 的父进程是驻留的 `WindowsTerminal.exe`，不是 `wt.exe`；用「父进程 + 创建时间 + 唯一命中」定位，`TerminateProcess` 后 tio 消失、终端窗口保留 |
| 后端探测 | `wt=1 version=1.25.2733.0 conhost=1`（版本从 `HKCU\…\AppModel\Repository\Packages` 读，不用启动终端） |
| 相对路径缺陷 | 用相对插件目录跑 L1 时 wt 路径失败（wt 把 tio 路径交给按自己工作目录解析的命令行）→ `Controller::configure` 现在强制转绝对路径；转完后同一测试通过 |
| 分组行 id 随语言变化 | 分组标题是本地化的，id 却是从标题派生的，切语言后宿主找不到原来那一行 → id 改成固定值（`group:launch` / `parameters` / `information`），L0 与 L2 各有一条断言 |
| 启动用的配置不落盘 | `launch()` 只 adopted 不写文件，于是「跑的是 A、下一次 query 显示 B」→ 现在启动前先写盘，L2+ 核对了 `tio.json` 的 `port` / `packageVersion` |
| 运行中的行没有任何提示 | 启动后该行与空闲时长得一样 → 运行/启动中/停止中会写进副标题 |
| 宿主设置被忽略 | `settingsChanged` 以前直接丢弃载荷 → 现在作为**种子**：只有 `tio.json` 没记过的键才采纳，用户自己的编辑优先（L2 覆盖，含「数字字段以字符串传入」） |
| 主题/语言切换不重绘窗口 | `DesignTokens.dark` 只读签名属性，且 `appearanceChanged` 不通知视图 → 改成读模型的 `dark` 并 `Controller::touch()` 通知，不重新探测端口 |
| 窗口渲染 | `--window` 在 software 渲染下截图确认：五个分组、控件、双语、深浅都正确（截图见会话记录，不入库） |
| 图标 | 字形方案按 [winshun-plugin-icons](../../winshun-plugin-icons) 流水线生成：Phosphor `wave-square`（U+EA9E）逐尺寸栅格化；`app.ico` 8 档 + `icons/app-*.png` 与 ICO 同像素；窗口标题栏/任务栏用 `setPluginWindowIcon()` 读 `:/plugin-icons` 资源（L2' 的无 QML 警告运行包含该路径）；字体加载从 `prepareGui` 挪到 `startGui`（`QGuiApplication` 之后），修掉了字体代码上线前的必然崩溃 |
| 无 Qt 回退 | 清空 `WS_QT_DIR`/`WS_APP_DIR` 后 `tio.exe` 仍然服务协议，`openWindow` 回答「独立窗口需要 Qt 版插件」，不崩 |
| 各契约版本 | `plugin.json` 声明 `"qt": "host"`, `"qtVersion": "6.12.0"`；宿主 `Qt6Core.dll` 实测 6.12.0.0 |

## 3. 还没跑（交付门槛）

这些**没有**验证过，不要在发布说明里写成已通过：

| 项 | 为什么没跑 | 影响 |
|---|---|---|
| DTR/RTS 四段真机波形 | 需要示波器与可控负载 | **阻塞交付**：README 与窗口里因此只写「未实测」，不写「保持/安全」 |
| 宿主内打开窗口的人工检查 | L3 已经用 `openWindow` 动作把窗口拉起来了（含「窗口开着时协议仍能应答」），但没人用眼睛看它在宿主里的样子 | 宿主里的 DPI、主题跟随与截图仍需人工过一遍 |
| 拔插误连 | 需要反复插拔 COM8 | 身份快照与「同号新设备不接管」只有逻辑测试，没有真机 |
| 真实端口占用 | 需要一个占着 COM8 的进程 | 上游原文 `Could not open tty device` 已实测，但「占用」与「不存在」是否可区分未测 |
| Unicode / 空格安装路径 | 未装到那种路径 | wt 的 `;`/`|` 拒绝逻辑有测试，Unicode 路径会经过 `wide()`，但没真装过 |
| 高号端口（COM10+） | 本机只有 COM8 | 解析与映射有测试，真机没有 |
| 干净机闭包 | 本机装着 MSYS2 | **阻塞发布**：不能让「本机恰好有 MSYS2」掩盖缺 DLL |
| 性能标定 | 未测 | `memoryMB: 128`、`timeoutMs: 1500` 是估算值，不是测量值 |
| 逐组件许可证 notice | 只放了原文，没做 notice 汇编 | **阻塞发布**：GPL/LGPL 义务需正式文本，见 [THIRD_PARTY](../THIRD_PARTY.md) |
| 200% DPI / 跨屏 | 未测 | 窗口会跟随 Qt 缩放，但没有截图证据 |
| 读屏 | 未测 | QML 控件有默认 `Accessible`，但没在 Narrator 下走过 |

## 4. 已知缺陷与已修

| 缺陷 | 状态 |
|---|---|
| 插件目录用相对路径时 wt 后端起不来 | **已修**：`Controller::configure` 调 `GetFullPathNameW` 转绝对路径（L1 回归覆盖） |
| `GetWindowTextW` 缓冲区少一个字符导致读不到端口框的值（原生窗口期的缺陷） | 随原生窗口一起删除，不再存在 |
| 共享 Qt 缺失时整机不能起插件 | **已修**：`tio.exe` 在找不到 Qt 时降级为无窗口协议服务，搜索框仍可用 |
| `TioApp.dll` 被占用时链接失败（`LNK1104`） | 非代码缺陷；改窗口前关掉预览窗口即可，已记在 [实现笔记](implementation.md) |

## 5. 怎么复现

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build\msvc -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=D:/tools/Qt/6.12.0/msvc2022_64
cmake --build build\msvc

build\msvc\tests\tio_tests.exe
build\msvc\tests\tio_window_tests.exe          :: 需要 Qt（DLL 会被复制到它旁边）
build\msvc\tests\tio_native_probe.exe        :: 会真起进程，注意先释放串口
python tests\integration.py
python tests\window_check.py                  :: 需要一个 Qt 6.12.0 目录
python tests\host_smoke.py                    :: 需要装好的 Win顺
python tools\package.py --dev
```
