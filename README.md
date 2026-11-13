# ra9fael.tio — Win顺 串口终端插件

用装在插件包里的 [tio](https://github.com/tio/tio) 打开串口终端。**在搜索框里就能改完参数并启动**，也可以开一个独立窗口配置。

当前版本：**v0.1.4**。

## 在线交互演示（不用安装）

独立窗口有一个**可交互的网页演示**：端口下拉显示设备管理器同名的设备、选中端口自动采集身份、可折叠的设备详情卡——全部可以在浏览器里试：

> **[打开交互演示](https://ra9fael.github.io/winshun-tio/)**（GitHub Pages，推送 `mockup/` 后自动发布）

如果 Pages 尚未生效，也可以用免配置的在线预览：
`https://htmlpreview.github.io/?https://github.com/ra9fael/winshun-tio/blob/main/mockup/index.html`

说明：这是**界面模拟稿**，数据是写死的真实设备样例（COM8/COM9），不连接串口、不代表运行中的插件；窗口本体的行为以安装后的插件为准。

> **远程共享是显式的双向控制，不是只读日志。** **默认开启**，端口默认 TCP `54321`，可在插件设置或独立窗口改为 `1024–65535`；tio 在所有网卡监听。局域网客户端可以读取串口数据，也可以向设备写入任意字节。WT 与远程客户端共享同一会话；只在可信网络中启用。独立窗口自己也会连到 `127.0.0.1:<端口>`，只收不发地显示本次共享收到的串口内容（最近 128 KB）。搜索框启动不会开启共享。

## 窗口截图

![tio 独立窗口：会话状态、双向共享、HEX 内容与日志历史](https://github.com/user-attachments/assets/f6ec794f-aa0a-4332-b44d-0002d973581c)

> 截图来自实际运行的独立窗口。共享串口内容卡可切换终端/HEX、设置每行字节数、显示解析列；自动日志默认开启，可从「日志历史」打开。

## 安装

- **插件库**：Win顺 → 设置 → 插件 → 插件库，搜索「tio」
- **本地包**：从 [Releases](https://github.com/ra9fael/winshun-tio/releases) 下载 `.wsplugin`，在「已安装」里从压缩包安装
- **手动**：解包到 `%LOCALAPPDATA%\WinShun\plugins\ra9fael.tio\`（`plugin.json` 在目录根部），重启 Win顺

首次启用会征求这些权限：`processes`（启动终端）、`ui`（显示窗口）、`clipboard:write`（复制共享内容、端口或诊断信息）；双向远程共享另申请 TCP `1024-65535` 的监听权限。插件升级新增权限时，Win顺会再次请求同意。需要 Win顺 **0.22.0** 及以上（共享 Qt 契约）。**不需要安装 MSYS2**：tio 及它真正导入的 DLL 都在包内（8 个文件，6.6 MB）。

## 用法

### 路径 A：搜索框（不打开窗口）

输入 `tio` 回车进入命令，插件给出一组行：

```text
启动（每个串口一行）
  ⏻  tio COM8
     COM8 → /dev/ttyS7 · 115200 8n1 · none · Windows Terminal
  ⏻  tio COM9
     COM9 → /dev/ttyS8 · 115200 8n1 · none · Windows Terminal
参数（Enter 修改，就地生效）
  ⌁  端口        COM8
  ⌁  波特率      115200
  ⌁  数据位      8
  ⌁  校验        none
  ⌁  停止位      1
  ⌁  流控        none
  ⌁  终端后端    Windows Terminal
信息
  !  控制线 DTR / RTS   默认行为未实测 · tio 无此 CLI 选项
  ▤  诊断              展开诊断（Shift+Enter 复制）
```

| 行 | Enter 做什么 |
|---|---|
| 启动行（每个端口一条） | 过了六道检查就用**这一行自己的端口**起 tio；该端口正在运行就按 Enter 停止那个会话 |
| 端口 / 波特率 / 终端后端 | 列表就地替换，Esc 退回，选中打勾 |
| 数据位 / 校验 / 停止位 / 流控 | 就地循环下一个值 |
| 诊断 | **展开诊断视图**：报告逐行显示在列表里，Esc/返回退回主列表；`Shift+Enter` 复制整份报告 |

端口不再藏在参数里：枚举到几个串口就有几条启动行，Enter 直接用它启动，不用先进「端口」子列表。启动行只带这一行的端口，波特率、校验、终端后端仍取参数里当前的值。

改参数**立刻写盘**，搜索框里没有「保存」按钮。启动时也会先写盘：跑起来的那份配置就是下一次查询看到的那份。plugin.json 的设置页只提供**默认值**——某个键在 	io.json 里还没记过时才采纳，你在这里改过的以这里为准。

### 路径 B：独立窗口

从结果列表里自己的「**打开独立窗口**」行（Enter）进入，或启动行的菜单（Ctrl+Enter）。

同样的分组，多出来的只有窗口放得下的东西：页签、「端口与参数」卡（端口下拉 + 刷新 + 波特率/数据位/校验/停止位/流控）、设备身份、会话卡和诊断文本。会话卡的高级区域可确认启用双向远程串口共享；会显示 TCP 端口 `54321`，明确提示所有网卡可访问、远程客户端可向串口写入，以及慢客户端可能拖住 tio。启动、重启时都需明确确认远程共享。

「＋」直接新建一个会话，端口留空到「端口与参数」里再选；一个端口只在会话运行中独占，选同一个端口不会被拦，点启动才会说明原因。远程共享只对本次独立窗口会话生效，不写回默认配置；重启或重新打开窗口后保持关闭。

窗口画自宿主的 `nativeLook` 契约：fluent / brand / tactile / liquid 四种风格、圆角开关、界面大小、透明效果自动跟随宿主，改外观实时重绘。

关窗时若会话还在跑，会给三个选择：继续在后台 / 停止会话并关闭 / 取消。

### 终端后端：用户可选，wt 优先，conhost 兜底

| | `wt.exe` | `conhost.exe` |
|---|---|---|
| tio 的父进程 | 不是我们（`wt.exe` 转交后退出） | 就是我们 |
| 停止方式 | 靠标题 token + 创建时间定位后定向终止 | owned PID + 创建时间 |
| 归属 | **受限**，窗口里会写明 | **自有** |
| 窗口 | Windows Terminal 管 | 随 tio 退出消失 |

没装 Windows Terminal 的机器自动用 conhost；选了哪台机器没有的后端会被忽略，UI 里标成不可选并说明原因。

### COMn → /dev/ttyS(n-1)

固定约定：COM1 → `/dev/ttyS0`，COM10 → `/dev/ttyS9`，COM256 → `/dev/ttyS255`。名称解析成功不等于端口存在；不存在会让 tio 自己报 `Could not open tty device`。

## 为什么没有 DTR/RTS 开关

锁定的 tio 3.9 **没有**设置 DTR/RTS 的命令行选项（`tio --help` 已核对）。所以这里没有可选的模式，也不虚构一个。打开端口、运行、正常停止、异常退出各阶段的电平变化还没有实测，本插件不声称默认行为是「保持」或「安全」，也不承诺打开端口不会产生控制线瞬态。真机波形实测仍是交付门槛。

## 文档

| 文档 | 内容 | English |
|---|---|---|
| [设计与需求](plugin-design.md) | 11 节设计、稳定需求 ID、优先级和验收 | [Design](plugin-design.en.md) |
| [UI 规格](UI-SPEC.md) | 启动表单、参数、键盘、状态与生命周期 | [UI specification](UI-SPEC.en.md) |
| [测试计划](TEST-PLAN.md) | L0–L5、依赖闭包、真机串口与门槛 | [Test plan](TEST-PLAN.en.md) |
| [第三方组件](THIRD_PARTY.md) | 逐文件来源、许可证与「为什么需要」 | [Third party](THIRD_PARTY.en.md) |
| [实现笔记](docs/implementation.md) | 代码结构、两条路径、构建与验证 | [Implementation](docs/implementation.en.md) |
| [验证记录](docs/validation.md) | 跑过什么、结果如何、什么还没跑 | [Validation](docs/validation.en.md) |

## 构建

需要 MSVC（VS 2022/2026 皆可）、CMake 3.21+、Ninja，以及 Qt **6.12.0**（本机 `D:\tools\Qt\6.12.0\msvc2022_64`）：

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build\msvc -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=D:/tools/Qt/6.12.0/msvc2022_64
cmake --build build\msvc
```

产物：`tio.exe`（无 Qt 的引导器）、`TioApp.dll`（跑 Qt 与 QML 窗口）。

### 开发期直接打开 GUI（稳定前，不打 `.wsplugin`）

```powershell
# 默认启动隔离构建目录里的可见窗口，使用临时 dataDir，不启动串口会话
pwsh tools\remote_share_gui.ps1

# 无窗口自检：启动、保持数秒、检查 QML 警告后退出
pwsh tools\remote_share_gui.ps1 -Check

# 保留本次临时配置，便于检查 tio.json / 会话目录
pwsh tools\remote_share_gui.ps1 -KeepData
```

窗口打开后，在「会话 → 高级 · 双向串口共享」中确认开关和 `54321` 端口；`Launch` 与共享会话的 `Restart` 会再次要求确认。包装器只负责打开窗口，不自动运行 tio，也不会写入仓库根目录的旧二进制或用户的正式配置。

```bat
build\msvc\tests\tio_tests.exe          :: 280 项纯核心断言（无 Qt、无 Windows）
build\msvc\tests\tio_native_probe.exe   :: 30 项真机检查（会真起进程）
python tests\integration.py             :: 70 项协议往返（加 --launch 再多 6 项真启停）
python tools\package.py --dev           :: 打 .wsplugin 并给 SHA-256
```

## 许可

插件本体 MIT。包内的 tio 与 MSYS2 运行时是 GPL-2.0-or-later，逐文件来源与义务见 [THIRD_PARTY.md](THIRD_PARTY.md)。
