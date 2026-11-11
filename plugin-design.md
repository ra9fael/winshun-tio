# tio 启动器设计与需求
[English](plugin-design.en.md) · [索引](README.md) · [UI](UI-SPEC.md) · [测试](TEST-PLAN.md)

## 1. 关键裁决


本插件只负责可追溯地打包并启动上游 tio，不实现自己的串口终端。确定约定为 **COMn → /dev/ttyS(n-1)**；无需再论证此映射。用户机器不需 MSYS2。外部 terminal/mintty 的选择必须经过真实宿主与干净机探针，不能从“exe 可启动”推导“交互和生命周期已可用”。**首版已实现**：搜索框（路径 A）与独立窗口（路径 B）两条启动路径共用一份状态；未跑的验证项见 [验证记录](docs/validation.md)。P0 核心必需，P1 首版交付必需，P2 可选。下表为稳定需求登记，ID 不复用。
|---|---|---|
| ID | 优先级 | 需求与可观察验收 |
|---|---|---|
| TIO-001 | P0 | 角色与独立性：独立安装包，可自行枚举/输入 COM 端口；缺失 device 不影响启动；不提供自绘终端、串口收发解析或驱动修改功能。 |
| TIO-002 | P0 | 运行时闭包：从锁定 MSYS2 包提取 tio.exe 与必要递归/动态加载 DLL，干净机无 MSYS2/bash/pacman/开发 PATH 可运行；遗漏或架构不匹配启动前报告并阻止连接。 |
| TIO-003 | P0 | 版本与校验：记录包名、上游/打包版本、仓库来源、架构、源包、下载校验依据及每文件 SHA256；篡改/混版/缺件禁止运行，不静默联网替换。 |
| TIO-004 | P0 | 映射与参数：COM1/10/256 等按 n-1 输出；拒绝 COM0、负数、非整数、溢出和命令注入；参数结构化，空格/Unicode 路径不破坏 argv，不经 shell 拼接。 |
| TIO-005 | P0 | 终端后端：探针核验外部 terminal/mintty 的 TTY、键盘、Unicode、退出码和窗口/进程归属后才选定；无可用后端显示可行动错误，不隐式安装或伪报连接成功。 |
| TIO-006 | P0 | processes/ui 与权限：仅普通 normal，声明 processes、ui；不用 elevated、UAC 或驱动操作；未同意权限时不启动子进程/窗口。 |
| TIO-007 | P0 | 生命周期：管理每次会话唯一 ID、启动/退出/失败和进程树；关闭、取消、停用、崩溃、宿主退出能回收拥有的子进程和句柄，不终止用户其他终端，不通过 breakaway 脱离 Job。 |
| TIO-008 | P0 | 串口占用：不以预先打开端口探测造成竞争；tio 报 busy/denied 时显示实际原因与重试入口，不杀占用者、不自动重复连接。 |
| TIO-009 | P0 | DTR/RTS：首次配置清晰显示锁定 tio 版本经核实的实际默认电平、流控及启动/退出行为，并保存版本关联配置；后续正常启动复用配置，不要求每次选择或额外确认。高级控制线设置仅由用户显式更改；按版本能力与真机波形核验，不支持则阻塞相关自定义模式，未知不得标为“保持/安全”；实测未完成阻塞交付。 |
| TIO-010 | P0 | 拔插防误连：启动前重新确认端口与可读设备身份；设备移除或 COM 号复用使会话失效并停止，禁止按同名端口自动重连，需用户重新选择并确认。 |
| TIO-011 | P1 | 表单与可访问性：波特率、数据位、校验、停止位、流控、控制线和后端清晰校验，全键盘、DPI/主题/读屏可用；不接收任意 shell 或未经审核的高级参数。 |
| TIO-012 | P0 | 许可证与来源：tio、MSYS2 runtime、每个 DLL、mintty（若分发）及启动器/UI 依赖逐项核验 license/source/notice/再分发义务，未关闭任何必需项不交付；源码链接不自动等于履约。 |

| TIO-014 | P2 | 可选联动：通用跨插件调用证据、目标可用与用户主动选择齐备后才接收设备入口；当前待验证，无依赖、无编造协议和自动连接。 |

| 证据 | 核读事实 | 边界 |
|---|---|---|
本轮仅核读本地工作台与必要协议/源码，没有下载 MSYS2 包、启动 tio、枚举串口或执行探针。
| [resident 协议:226](../WinShun/docs/plugins.md) | stdio JSON-RPC、设置变化、取消与 shutdown | tio stdout 不能混入插件协议 |
| 证据 | 核读事实 | 边界 |
|---|---|---|
| [工作台规范:40](../DEVELOPMENT.md) | 11 节、双语、L0–L5 和 L3 门槛 | 不等于已批准实施 |
| [resident 协议:226](../WinShun/docs/plugins.md) | stdio JSON-RPC、设置变化、取消与 shutdown | tio stdout 不能混入插件协议 |
| [权限:402](../WinShun/docs/plugins.md) | processes 控制起子进程，ui 用于自有窗口，normal 普通权限 | 终端在该 Job 内能否交互仍未测 |
| [runner:495](../WinShun/src/plugin/runner/main.cpp) | 读取 processes/ui 并创建 Job | 不是外部终端兼容结论 |

| [窗口协议:269](../WinShun/docs/plugins.md) | resident+ui、appearance、settingsWindow | UI 技术待选 |
| [Session.cpp:115](../WinShun/src/plugin/Session.cpp)、[宿主工具:265](../WinShun/docs/plugins.md) | winshun/tool 分派与已列工具 | 无通用插件互调证据，不能推导 device→tio API |


用户给定事实/裁决：COMn→/dev/ttyS(n-1) 为确定约定；从 MSYS2 提取运行时打包，干净机不需要 MSYS2，不做自有终端。未核读任何具体上游包许可证，不宣称其 SPDX、版本或 DLL 名单已确定。

## 3. 架构与 manifest 文字草案
## 4. 核心逻辑


未来包布局为 plugin.json、bin/启动器、runtime/tio.exe 与必要 DLL、可选 terminal/mintty 运行时、licenses/逐组件声明、sources/源码履约材料或适当取得说明和版本哈希清单。仅为文字职责草案，本轮不创建配置或目录。DLL 发现从 PE imports 递归开始，补充 delay-load、动态加载、实际运行轨迹；区分 Windows 系统 DLL 与必须随包组件，不能复制整个 MSYS2 或凭一次启动裁剪。控制私有 PATH/cwd，防环境中的其他 MSYS runtime 混入；不修改系统/用户 PATH，不加载 dataDir 代码。

manifest 文字草案：manifest=1，id=ra9fael.tio；双语 name/description/commands/settings；版本、license、minWinShun 待核定。resident、包内 exe、normal、lazy；权限 processes、ui，files:write 仅在明确的自身设置/诊断写入方案中声明；不申请 network/elevated。命令稳定 ID tio，候选关键字 tio；动作 configure、launch、stop、diagnostics。query 仅展示候选会话，不打开端口；action launch 由用户主动点击启动，复用已保存配置，不增加常规启动确认面板。initialize 获取 dataDir/设置/外观；settingsChanged 仅改变后续会话；shutdown 先停止自身会话和等待回收再答复。协议 stdout 与子进程 stdio 完全隔离，stderr 只记脱敏诊断。

## 5. 交互

## 4. 核心逻辑

TIO-004：规范化 COM 名称可接受大小写，展示统一 COMn；n 为正十进制整数，核心用检查过的范围转换 n-1，超界拒绝。COM10→/dev/ttyS9，COM256→/dev/ttyS255。可解析不等于本机该高号端口存在；不存在须报告，不改变映射。所有 Windows 路径与 Unicode 文本使用明确编

[Output truncated. Use view_range=[56, ...] to continue reading. In your next response, you may batch this with other view calls. File has at least 117 lines.]
|---|---|---|
| tio.exe | 精确版本、原文许可证、版本号、NOTICEE、二进制再分发条件、对应源码版本、获取方式、修改记录 | 未验证，关闭后可分发 |
| MSYS2 runtime | 选定该文件的许可证及例外、对应源码和构建材料义务、可用期/提供方式 | 未验证，不能只附普通仓库链接 |
| 每个必要 DLL | 单独版本/来源/哈希、license 原文、归属、源码 noticee/链接或可替换条件（若适用）| 每项未验证，任何缺项阻塞 |
| mintty（若随包）| 精确版本、许可证、对应源码、修改和再分发条件及自有 DLL closure | 未验证，不分发则记录不适用理由 |
| 外部 terminal（不随包）| 发现/调用方式、使用限制；如改为分发重新核实所有义务 | 未验证，不把本机存在当可再分发 |
| 启动器、SDK、UI 依赖 | 自有许可选择、第三方 notice、静/动态链接与源码或重链接条件（若适用）| 未验证，逐组件批准 |
| .wsplugin 整体 | notices/许可证原文随包可读，对应源码/适用书面提供条款可履行，来源与二进制版本对应 | L0/L5 全项关闭，未履约不发布 |

## 7. 失败矩阵

| 条件 | 表现 | 降级/恢复 | 需求 |
|---|---|---|---|
| DLL 缺失/架构不符/哈希不符 | 验证失败，不开端口 | 指定组件和重装建议，无自动下载 | TIO-002/003 |
| 外部 terminal 缺失或 TTY/Job 不兼容 | 无可用后端 | 选择另一个已验证后端；均无则阻塞启动 | TIO-005/007 |
| 空格/Unicode/注入式参数 | 合法路径正常，非法参数校验失败 | 保留输入并定位错误，不走 shell | TIO-004/011 |
| busy/denied/无端口 | 独立原因，未连接 | 用户处理后主动重试，不杀占用进程 | TIO-008 |
| 高级控制线模式不支持/行为未知 | 该模式不可选，说明原因，不标“保证安全”| 复用已核实并保存的配置；默认行为与各提供模式实测未完成阻塞交付，不增加逐次确认 | TIO-009 |
| 拔插/COM 号复用、身份不确定 | 设备移除或需重确认 | 停自身会话，重新选择，不自动重连 | TIO-010 |
| 部分进程创建失败/终端立即退出 | 启动失败含退出信息 | 回收拥有的部分进程和句柄 | TIO-007/013 |
| 宿主停用/崩溃/退出 | 会话终止或清理 | Job 兜底，无 orphan，不结束用户其他终端 | TIO-007 |
| device 缺失/互调未知 | 联动待验证，不可用 | 自行选择端口仍可用 | TIO-001/014 |
| 任一 license/source 义务未闭合 | 交付阻塞 | 完成材料或去掉组件并重验证闭合 | TIO-012 |

## 8. 交付清单

本轮仅八份双语需求文档，不写实现、脚本、配置，不提交。未来交付包括双语 README/CHANGELOG/发行说明、manifest、独立 .wsplugin/SHA256、锁定包与文件来源清单、tio 与最小已验证 DLL closure、选定终端方案、逐组件许可证与源码履约材料、参数与控制线说明、清理策略和证据。架构及最低 Windows/宿主版本待探针确认，不虚填已支持版本。不发布到插件库，直到 L3/L4 与许可门槛满足。
## 9. 测试计划 L0–L5

见 [测试表](TEST-PLAN.md)。L0 静态包/manifest/哈希/许可证；L1 跨平台 COM/argv/参数/状态机；L2 fake OS/终端/进程/身份与故障注入；L3 真实宿主 normal 下 processes/ui、协议和 Job 往返，失败不算完成；L4 解锁桌面真实串口、控制线测量、终端输入与拔插，失败阻塞首版交付；L5 干净机闭包、性能标定与发布核对。L0/L1/L2 前三层本轮已跑通，L3 只到协议层，L4/L5 与波形实测未执行——逐项见 [验证记录](docs/validation.md)。
交互时延候选待测目标：缓存查询 p95 ≤ 200 ms；点击启动到终端可交互 p95 ≤ 2 s；停止后 owned 子树退出 ≤ 2 s，均非已通过结果。工作集不预设上限：实测启动器空载稳定基线 B，以及启动、活动会话、诊断、停止和失败清理等负载峰值 P，按 B + (P−B) + 有依据的余量 M 推导预算；记录实测值、场景、增长趋势和余量理由后才标定。启动器、tio、终端和外部 broker 分列并报告会话整体峰值，不能掩盖生命周期失败。记录硬件/OS/宿主/后端版本、Defender、冷/热态和采样方法；重复次数由场景覆盖、波动及 p95 估计精度决定，披露实际样本量和不确定性，不预设无依据的固定次数。
## 10. 诚实笔记

实测：没有运行实测；仅核读第 2 节本地文件，初始仓库只有 Git 元数据。用户已确定 COM 映射与产品边界。推断：resident normal+processes/ui 可承载启动器，现有 Job 约束支持退出清理意图。未验证：MSYS2/tio 版本、DLL 名单与动态 closure、所有许可证/source 义务、后端、DTR/RTS 实际波形、占用错误解释、COM 高号真机、Unicode/空格路径、宿主退出和 broker 清理、UI/最低版本、性能以及跨插件调用。不能把静态核读写当成 Windows 已验证。
## 11. 实施后回填
未实施，暂无结果。未来逐版记录版本/提交、包/源码/文件哈希、组件义务闭合表、后端裁决与探针证据、OS/宿主/硬件、需求偏差、L0–L5 实际结果、未测边界、性能标定、权限变化、清理与发布决定。证据计划在 `tio-</日期>/`，本轮不创建验证产物。