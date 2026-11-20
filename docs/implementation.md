# 实现笔记

[English](implementation.en.md) · [索引](../README.md) · [验证](validation.md)

## 1. 代码结构

两条路径共用一份状态和一套核心。分成这几层，是为了让**纯逻辑能在没有 Windows、没有 Qt 的机器上跑测试**。

| 文件 | 层 | 内容 |
|---|---|---|
| [logic.h](../logic.h) | 纯核心 | COMn 解析与 n−1 映射、参数校验、argv 引用、后端偏好与解析、启动计划、会话状态机、`ConfigKey`/`FormEdits`、六道门。**零 Windows 依赖** |
| [rows.h](../rows.h) | 纯核心 | 行模型：`Row`、`Lang`、候选行构造、后端选择标签。只依赖 `logic.h` |
| [native.h](../native.h) | Win32 | 只读枚举（SetupDi + SERIALCOMM）、SHA-256 + PE 架构校验、后端探测、启动/停止、私有 PATH |
| [controller.h](../controller.h) | 状态 | `Form` / `Snapshot` / `Controller`：配置、刷新、保存、启动、停止、诊断。单 mutex + `Notify` 回调 |
| [core.h](../core.h) | 状态 | `Controller` 的进程级实例，以及 `makeLang()`（双语字符串表） |
| [rows_build.h](../rows_build.h) | 路径 A | `buildRows(snapshot, browse, lang)`：把快照变成行 |
| [plugin.h](../plugin.h) | 路径 A | 协议实现：`query` / `action` / `details` / `settingsChanged` / `shutdown` |
| [probe.h](../probe.h) | 入口 | `--probe ports / backends / verify` 只读诊断 |
| [window_hook.h](../window_hook.h) | 接缝 | 协议要开窗时调的钩子；控制台版没人装，于是提示需要 Qt 版 |
| [qmlapp/stub/main.cpp](../qmlapp/stub/main.cpp) | 入口 | `tio.exe` 真正的 main：找 WinShun 的 Qt、加载 `TioApp.dll`；`--probe` 与 `--serve` 留在本进程 |
| [qmlapp/qmlmain.cpp](../qmlapp/qmlmain.cpp) | 路径 B | DLL 入口：GUI 线程起 Qt，协议跑 worker 线程，装窗口钩子 |
| [qmlapp/TioWindow.h](../qmlapp/TioWindow.h) / [.cpp](../qmlapp/TioWindow.cpp) | 路径 B | 暴露给 QML 的模型：`Q_PROPERTY` 读、`Q_INVOKABLE` 改 |
| [qmlapp/qml/](../qmlapp/qml/) | 路径 B | `TioWindow.qml` + `GroupCard` / `FieldRow` / `DesignTokens` / `TokenComboBox` / `TokenSegmented` / `TokenButton`（`TokenSegmented` 用外观契约画宿主同款的分段单选，承载取值少的参数行） |

**为什么 `tio.exe` 没有 Qt**：对齐 [ra9fael.qr](../../ra9fael.qr) 与 [ra9fael.binarylab-qml](../../ra9fael.binarylab-qml) 的做法，也是 WinShun 自家 `winshun.capture` 的做法。`plugin.json` 声明 `"qt": "host", "qtVersion": "6.12.0"`，宿主在 `CreateProcess` 前设好 `WS_QT_DIR`；`tio.exe`（静态 CRT）把它加进 DLL 搜索目录，再 `LoadLibraryExW(TioApp.dll)`。好处是包内不重复分发 Qt，也不会混入另一版 Qt 覆盖共享路径。

**没有 Qt 也要能用**：找不到 `WS_QT_DIR` / `WS_APP_DIR`（或那里没有 `Qt6Core.dll`）时，`tio.exe` 不退出，而是直接在无窗口模式下跑协议——搜索框这条路本来就不需要 Qt。此时 `openWindow` 动作回答「独立窗口需要 Qt 版插件」，不假装打开。

## 2. 路径 A：搜索框的一次往返

```
WinShun                          tio.exe (protocol thread)
  query ───────────────────────►  TioPlugin::query
                                    buildRows(snapshot(), Browse::None, lang)
  ◄───────────────────────────── items[]  （启动行 + 波特率行 + 开窗行）
  action "openBaudrates" ──────►  m_browse = Baudrates
  ◄───────────────────────────── {effects:[replace(items[])]}
                                    列表首行是 back，每行一个常用波特率
  action "baud"（item.id=baud:9600）►  form.serial.baudrate = 9600; controller.adopt(form, {Baudrate})
  ◄───────────────────────────── replace（主列表，波特率行已是 9600）
```

- 主列表刻意精简：每个串口一条启动行、波特率快捷项、一行「打开独立窗口」（同时是配置摘要）。端口行、后端列表、控制线说明与诊断视图都不在搜索框里——端口与完整参数在窗口里，诊断报告在窗口的「诊断」卡和 `details()` 里。
- `adopt(form, keys)` 只收列出的键：一次选择就是最终结果；其中宿主持有的键（端口/波特率/后端）经 `winshun/settings/update` 写回设置页，高级键写进 `tio.json`。
- `query` 每次都重读端口（`Controller::refreshPorts()`），并通过 `queryResult` 的 `rerun`（3 秒）请宿主隔几秒再问一次：插件是常驻的，运行期间插拔设备不会重启进程，所以行必须是机器此刻的端口。词没变时保留 `m_browse`——rerun 重发的是同一条查询，不是用户又打字了，这时把候选列表收掉会在用户正读波特率时把它抢走。
- `details()` 返回诊断文本：宿主的详情面板可以读整份报告，不用开窗。

## 3. 路径 B：窗口

```
winshunTioMain(appDir)                    GUI thread
  prepareGui()                             Qt 库路径 = WinShun 的
  installWindowHook(...)  ← 必须在 serve() 之前
  QGuiApplication app
  std::thread serve ──► runServeLoop()  ──► 与路径 A 完全相同的 TioPlugin
                                           （serve() 返回 → app.quit()）
  app.exec()
```

窗口本身是 `QQmlApplicationEngine` + `TioWindow.qml`，模型是 `tioplug::TioWindow`（`Q_PROPERTY` 读 + `Q_INVOKABLE` 改）。它的构造里给 `Controller` 装了一个 `Notify`，用 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 把刷新甩回 GUI 线程——`Controller` 的回调来自**任意**线程（比如协议线程正在执行的启动）。

`openWindow` 动作在协议线程上被调用，钩子用 `Qt::BlockingQueuedConnection` 把窗口创建搬到 GUI 线程，这样动作能如实回答「开没开成」。

### 窗口与搜索框不会打架

- 窗口编辑的是自己的 `m_form`，只有 `save()` / `launch()` 才写回 `Controller`，而且只提交用户改过的键（`FormEdits`）：没动过的字段不回写，外面的更新不会被旧副本盖掉。
- 脏标记来自 `m_edits.any()`：有未保存的编辑就脏。脏时外部变化不覆盖表单；切页签/新建/克隆/关闭会清编辑（编辑属于当时那个页签）。
- `save()` / `launch()` 先写配置再动作：屏幕上看到的就是要用的，也是搜回去会看到的。

### 配置键的两半归属

`logic.h` 的 `ConfigKey` 把九个键分成两半（`hostOwned()`）：

| 键 | 归属 | 存储 | 谁改 |
|---|---|---|---|
| `port` / `baudrate` / `backend` | 宿主设置页 | Win顺 的 INI（`tio.json` 不再保存） | 搜索框、窗口、宿主卡片 |
| `dataBits` / `parity` / `stopBits` / `flow` / `share` / `sharePort` | 插件 | `dataDir/tio.json` | 独立窗口 |

`controller.h` 的 `applySettingsLocked()` 是唯一判定点：

- **公共键**：宿主有记录就用宿主的（页面上的最新答案），没有才读文件旧值（升级安装），最后是内置默认。宿主 `settingsChanged` 推来的值立即采纳，且**不回发**（防回环）。
- **私有键**：文件优先，宿主值只作为旧清单遗留键的迁移来源（首次加载写入文件，`m_migrated` 一次完成）。
- **回写**：`publishLocked()` 只对三个公共键发**差异**，并对本地宿主状态做乐观更新——值没变不发，宿主也就不会回推一遍。回写走 `SettingsWriter` → SDK `updateSettings` → `winshun/settings/update`。
- **重播**：默认值变化只重播仍等于旧默认的空闲页签（`sameForm`），用户的定制和运行中的会话一律不动。

### QML 只用契约内的模块

`QtQuick`、`QtQuick.Controls.Basic`、`QtQuick.Layouts`——共享 Qt 契约保证这三个。**没有**用 WinShun 的私有 QML 模块（`WinShun/Theme.qml` 之类），也没用 `QtQuick.Window`：Win顺 安装目录里 `qml\QtQuick\Window` 是空的，`Window` 类型由 `QtQuick` 提供。

`DesignTokens.qml` 是单例（CMake 里 `QT_QML_SINGLETON_TYPE`），深色由 `darkTheme` 上下文属性决定；`qmlmain.cpp` 在独立运行时从注册表读 `AppsUseLightTheme`，在宿主内跟着 `appearance`。

## 4. 后端与进程归属

| | wt | conhost |
|---|---|---|
| `CreateProcessW` 目标 | `wt.exe -w new --title "tio COM<n> <token>" --suppressApplicationTitle -- <runtime>\tio.exe …` | `<runtime>\tio.exe …` |
| 标志 | `CREATE_UNICODE_ENVIRONMENT` | `CREATE_UNICODE_ENVIRONMENT \| CREATE_NEW_CONSOLE` |
| 找到 tio | `findWtChild()`：扫进程表，取 `tio.exe` 且父进程是 `WindowsTerminal.exe` 且创建时间 ≥ 启动时刻，**必须唯一命中** | 就是拿到的 PID |
| 记录 | PID + 创建时间 | 同 |
| 停止 | `stopOwned()` 先试 Ctrl-C，超时再 `TerminateProcess` | 同，但 tio 在我们创建的控制台里 |

三个不能省的细节：

1. **wt 路径不能带 `CREATE_NEW_CONSOLE`**：终端自己建控制台。
2. **conhost 路径必须带**：实测不带时 tio 打印 `Non-interactive mode enabled`，断开后立刻退出，不读键盘。
3. **wt 按 `;` 和 `|` 切分命令行**：插件路径含这两个字符时 `planLaunch()` 直接返回 `PlanError::PackagePath`，不试图转义。

`stopOwned()` 每次都比对创建时间：PID 会被复用，数字相同不等于同一个进程。

## 5. 六道门

`logic.h` 的 `check(Environment)` 是唯一判定点，UI 只显示结果：

| 门 | 失败条件 |
|---|---|
| `Port` | 名字解析不出来（不是 COMn、0、超界） |
| `Parameters` | 波特率 ≤ 0、数据位不在 5–8、停止位不是 1/2 |
| `Identity` | 枚举读不到该端口的 InstanceID |
| `Package` | 任一包文件哈希/架构不符 |
| `Backend` | 解析后是 `Backend::None` |
| `Session` | 已有会话在跑 |

顺序固定，所以启动行写的原因每次都是同一句。**参数行不受门影响**：包坏了也不能锁住界面。搜索框里只有 `Package` / `Parameters` / `Backend` 这类与端口无关的门会写进每一条启动行；`Port` / `PortBusy` / `Identity` / `Session` 属于单个端口或单个页签，由那次启动的 toast 报出——COM8 跑着，不影响 COM9 那一行照常启动。

## 6. 构建

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build\msvc -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=D:/tools/Qt/6.12.0/msvc2022_64
cmake --build build\msvc
```

产两个目标到仓库根：`tio.exe`、`TioApp.dll`。QML 走 `qt_add_qml_module`（URI `Tio`），`QT_RESOURCE_ALIAS` 把四个 `.qml` 摊平到模块根，这样组件之间按名字互相引用；`QT_QMLCACHEGEN_ARGUMENTS` 设为 `--only-bytecode`，不生成 C++，跟宿主哪版 Qt 都能跑。

`TioApp.dll` 不能覆盖正在运行的实例——改窗口代码前先关掉预览窗口，否则 `LNK1104`。

### 6.1 测试分层

| 层 | 命令 | 覆盖 |
|---|---|---|
| L0 | `tio_tests.exe` | 292 项：解析、校验、引用、后端偏好与解析、计划、会话机、六道门、`FormEdits` 键集、`sameForm`、行模型、主列表形状、窗口入口、外观契约 |
| L0' | `tio_window_tests.exe` | 125 项：窗口模型的取值表、按键提交的保存、自定义波特率、auto 偏好持久化、脏表单保护、后端可选性、诊断 |
| L1 | `tio_native_probe.exe` | 30 项：包校验、后端探测、枚举、两后端起停、创建时间、无控制台行为 |
| L2 | `integration.py` | 52 项：协议往返、波特率候选列表、`winshun/settings/update` 移交、宿主设置种子与迁移、主列表形状、窗口入口、语言、拒绝、重启 |
| L2+ | `integration.py --launch` | 59 项：另外真起一个会话并停止 |
| L2' | `window_check.py` | 28 项：中英 × 深浅 4 组 + 宿主四种风格 + 直角，无 QML 警告 |
| 开发期 GUI | `pwsh tools\remote_share_gui.ps1` | 可见窗口 + 临时 dataDir，手工检查共享确认/端口/状态；`-Check` 为无窗口自检 |
| L3 | `host_smoke.py` | 16 项：按宿主方式跑真实 DLL，含 `openWindow` |
| L4 | 真串口 | COM8 → `/dev/ttyS7` 能连、停后可再开 |
| L5 | 干净机 | 闭包复测（**未跑**） |

`tio_window_tests` 需要 Qt：CMake 把 `Qt6Core.dll` / `Qt6Gui.dll` 复制到测试 exe 旁边（`tools/copy_qt_dlls.cmake`），因为插件本身是从 Win顺 的目录加载 Qt 的，而普通测试 exe 是直接 import。
