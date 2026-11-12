# 会话回收：实现方案（插件侧）

> 关系：`docs/session-reclaim.md` 回答「为什么该由插件改」；本文回答「具体怎么改、改哪里、怎么验收」。
> 代码位置以 `0c3ec35` 为准；引用函数名优先于行号，行号只用于核对。

## 1. 结论

| 阶段 | 做什么 | 解决什么 |
|---|---|---|
| P1 | `dataDir\sessions\<sessionId>.json` 登记表 + 插件启动后异步回收 | 插件被强杀后，wt 里的孤儿 tio.exe 仍占着 COM 口；下一次插件启动把它收回 |
| P2 | 所有会话并行停止，总预算 ≤ 1200 ms | 宿主 shutdown 只等约 2 s，现有串行停止会超时 |
| P3 | 嵌套作业对象实验（可选，未实测前不做） | 从根上消掉「孤儿窗口期」 |

明确不做：不改宿主协议、不加 manifest 权限、不动 Argus 的残留行（那是 Argus 自己的缺陷，见分析稿第 2 节）。

## 2. 核对结果（对分析稿的修正）

| # | 分析稿说法 | 代码事实 | 对方案的影响 |
|---|---|---|---|
| 1 | 宿主 shutdown 等 2 s | `Host.cpp:38` `kShutdownGraceMs = 2000`；`winshun.h:960` 注释「约 2 秒」 | 成立，P2 的预算由此推导 |
| 2 | 串行停止 2 个会话 = 2.4 s | `stopMatching()` 串行调用 `stopOwned(pid, created, hostShutdown ? 1200 : 2000)`；`stopOwned` 优雅期后还有 `WaitForSingleObject(process, 1500)`（`native.h:954`） | **单会话最坏 1200+1500=2700 ms**，两个就是 5.4 s，比分析稿估计的更糟。P2 必须给整个操作设硬截止，不能只把 1200 传给 `stopOwned` |
| 3 | 「插件启动时回收」 | `plugin.json` `start: "lazy"`；`timeoutMs: 1500` 是**一次查询/动作**的上限（`WinShun/docs/plugins.md:157`），慢活要后台线程 | 回收**不能同步塞进初始化路径**：会拖慢/超时第一条查询。必须异步，并把「回收中」暴露给窗口 |
| 4 | pending 状态靠扫进程认领 | `findWtChild()`（`native.h:884`）只在**唯一命中**时返回 PID，多命中返回 0，注释明说「do not guess」 | 认领沿用同一原则：候选不唯一就不动，保留文件下次再收 |
| 5 | 读命令行取设备路径 | `tioArguments()` 的最后一个参数就是设备路径（`logic.h:355-365`），`planLaunch()` 原样拼进命令行 | 可行；需要新增 `ProcessCommandLineInformation` 读取（见 3.4） |
| 6 | 创建时间防 PID 复用 | `stopOwned()` 已比对 `created`（`native.h:930-931`） | 直接复用，不重复实现 |
| 7 | 写 `dataDir` 不需要新权限 | `settingsPathLocked()` 已经写 `dataDir\tio.json`（`controller.h:654-656`） | 成立；回收目录与它同级 |
| 8 | 插件 lazy，用户不开 tio 就不回收 | `plugin.json` `start: "lazy"` | 接受：孤儿只在用户下次用 tio 时才碍事，而那一刻插件必然已启动。文档写明这个边界 |

## 3. 设计

### 3.1 登记文件

位置：`<dataDir>\sessions\<sessionId>.json`，与 `tio.json` 同级。`sessionId` 用现成的 token（`newToken()`，`controller.h:641`），它已经出现在 wt 标签标题里（`logic.h:403`），便于人工核对。

```json
{
  "v": 1,
  "sessionId": "s-3f9a21c",
  "ownerPid": 1234,
  "ownerCreated": 131000000000000000,
  "com": 8,
  "device": "/dev/ttyS7",
  "backend": "wt",
  "pid": 85536,
  "created": 131000000123456789,
  "launchedAt": 1730000000000
}
```

| 字段 | 用途 |
|---|---|
| `ownerPid` + `ownerCreated` | 归属判定：owner 还活着且创建时间对得上 → 另一个插件实例在管，跳过 |
| `sessionId` | 文件名与诊断；也是 wt 标题里的那串 |
| `com` / `device` | 回收时核对设备，防止 COM 号被别的设备复用 |
| `backend` | `wt` 的进程是 WindowsTerminal 的孩子；`conhost` 的进程是插件子进程，作业兜底 |
| `pid` + `created` | 0 表示 pending（wt 还没交出 PID）；否则是会话进程 |
| `launchedAt` | pending 认领的时间下界，也用于限制认领窗口 |

写入必须原子：写 `<name>.tmp` → `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`。写一半被打死只会留下 `.tmp`，回收时忽略并清理。

### 3.2 写入时机

```
launch 流程（controller.h launchLocked 第 2/3 阶段）
  token/port/device 确定 ──► 写 pending（pid=0）
  beginStart(session)      ──► 视图显示 starting
  launch() 返回 pid/created ─► markRunning
                            └─► 重写文件：pid/created
停止（stopMatching / markExited）──► 删除文件
关闭页签（closeSession）    ──► 删除文件
插件退出（stopAll 完成）    ──► 删除本实例写过的全部文件
```

失败路径：`launch()` 失败（`markFailed`）时删除 pending 文件——没有进程可回收。

### 3.3 回收算法

插件启动后，在后台线程跑一次（`m_reclaiming = true`），窗口显示「正在回收上次的会话…」；`newSession` / `launch` 在回收完成前拒绝并说明原因，避免和回收抢端口。

```
for each file in <dataDir>\sessions\*.json（按 launchedAt 升序）
  1. 解析失败 / 版本不符 → 删文件，记一条诊断（坏文件留着只会一直报错）
  2. owner 存活（pid 在 且 创建时间一致）→ 跳过（另一个实例在管）
  3. pid != 0:
       a. pid 不在            → 进程已退出 → 删文件
       b. 创建时间对不上       → PID 被复用 → 删文件，不动它
       c. 镜像名不是 tio.exe   → 不是我们的 → 删文件，不动它
       d. 否则                → stopOwned(pid, created, 800)；确认退出后删文件
  4. pid == 0（pending）:
       扫进程表：tio.exe 且父进程 windowsterminal.exe 且 created ≥ launchedAt
                 且 created ≤ launchedAt + 120s
                 且命令行最后一个参数 == device
       唯一命中 → stopOwned(child, created, 800) → 删文件
       0 个或多于 1 个命中 → 不猜，保留文件（下次再收）
  5. 停止预算：每会话 800 ms 优雅 + 强杀；整体截止 4 s
     截止时仍未确认退出的会话：保留文件，诊断里记「下次再试」
```

判定表：

| owner | pid | 创建时间 | 结论 |
|---|---|---|---|
| 活 | — | — | 跳过 |
| 死/无 | 0 | — | pending 认领（见上） |
| 死/无 | 在 | 一致 | 回收 |
| 死/无 | 在 | 不一致 | 删文件，不动 |
| 死/无 | 不在 | — | 删文件 |

### 3.4 读别的进程的命令行（唯一新增 Win32 能力）

`NtQueryInformationProcess(ProcessCommandLineInformation = 60)`，两段式：先拿长度（`STATUS_INFO_LENGTH_MISMATCH`），再读 `UNICODE_STRING`。约 60 行，放在 `native.h`：

```cpp
inline bool processCommandLine(DWORD pid, std::wstring& out);
```

需要 `PROCESS_QUERY_LIMITED_INFORMATION`；读不到就返回 false，调用方按「不唯一」处理，不猜。

命令行解析只做一件事：取 `CommandLineToArgvW` 的最后一个参数，与登记文件里的 `device` 比较。tio 的参数顺序固定（`logic.h:355`），设备路径永远在最后。

### 3.5 并行停止与硬预算

现状（`controller.h:411-444`）：先收集受害者，再串行 `stopOwned`，最后统一 `markExited`。

改法：

```
1. 锁内收集 victims（pid, created, index），解锁
2. 每个 victim 一个 std::thread：stopOwned(pid, created, 800)
3. 用 QDeadlineTimer / steady_clock 设总截止 1200 ms：
   - 到点仍活着的，TerminateProcess(handle, 0xC000013A)，再等 300 ms
4. join 全部线程（线程内部自己保证不超过 ~1500 ms）
5. 锁内按真实结果标记：
   - exited       优雅退出
   - forced       强杀成功
   - gone         本来就不在了
   - failed       强杀也失败（诊断里红字，下次启动回收再试）
```

- `stopOwned` 增加一个「总截止」参数（或新增 `stopWithin(pid, created, deadline)`），把优雅期和强杀等待都算进同一个 deadline，避免 800+1500 的叠加。
- 语义不变：标记 exited 之前，进程必须真的没了；端口立即可再打开。
- 每个结果写进诊断视图，不再「无论成败都 markExited」。

预算推导：宿主 2000 ms − 写回答/进程退出 ≈ 100 ms − 余量 200 ms = 1700 ms；取 **1200 ms**，留 500 ms 给调度抖动。

### 3.6 触发时机与界面状态

| 时机 | 动作 |
|---|---|
| `initialized()` | 记下 dataDir；启动一次后台回收线程（只跑一次） |
| 回收进行中 | `TioWindow` 暴露 `reclaiming`；窗口顶部一条提示；`newSession`/`launch` 返回「正在回收上次的会话，请稍候」 |
| 回收完成 | 发一次 `changed()`；诊断行写「回收：n 个会话，x 个已停，y 个留待下次」 |
| `query`/`action` 首次到达 | 若回收未开始（懒启动边界），同样触发，保证用户看到列表前已开始回收 |

不做「每次 query 都扫目录」：一次启动扫一次即可，避免 1500 ms 预算被反复占用。

## 4. 代码落点

| 文件 | 改动 |
|---|---|
| `session_registry.h`（新增） | 文件结构、序列化/解析、决策函数 `decide(entry, probes)`（纯逻辑，可单测）、原子写 |
| `native.h` | `processCommandLine()`；`stopWithin()`（带总截止的停止）；`isImage(pid, L"tio.exe")` |
| `controller.h` | `writeSessionFileLocked()` / `deleteSessionFileLocked()`；launch 三处调用；`reclaimOrphans()`；`m_reclaiming` + 结果计数 |
| `plugin.h` | `initialized()` 启动回收；`query`/`action` 的回收中短路；诊断行接线 |
| `qmlapp/TioWindow.h/.cpp` | `Q_PROPERTY(bool reclaiming)`、`reclaimSummary`；`newSession` 的拒绝提示 |
| `qmlapp/qml/TioWindow.qml` | 顶部回收提示条；新建/启动按钮在回收中禁用 |
| `rows.h` / `rows_build.h` | 诊断里加一行回收结果 |
| `tests/` | 见第 5 节 |

## 5. 测试

### L0（`tio_tests.exe`，纯逻辑）

| 用例 | 断言 |
|---|---|
| 登记文件往返 | 序列化 → 解析 → 字段一致 |
| 坏文件 | 解析失败返回空，决策为「删」 |
| owner 存活 | 决策为「跳过」 |
| PID 复用 | 创建时间不一致 → 「删，不动」 |
| pending 无候选 | 保留文件，不产生停止动作 |
| pending 多候选 | 保留文件，不猜 |
| pending 唯一命中 | 停止动作带正确的 pid/created |
| 命令行解析 | 末参数 `/dev/ttyS7` → COM 8；参数带引号/空格也能取对 |

### L1（进程级，`tests/reclaim_check.py`）

| 编号 | 场景 | 断言 |
|---|---|---|
| R1 | 启动会话 → 强杀插件 | 会话 tio.exe 仍在（wt），COM 口被占 |
| R2 | 重新启动插件 | 孤儿被停，`sessions\` 清空，COM 口可立即再开 |
| R3 | 两个插件实例 | 后启动者不回收前者的会话 |
| R4 | 伪造登记的 PID | 指向非 tio.exe 的进程不动，只删文件 |
| R5 | 两个会话 + shutdown | 2 s 内完成清理并回答 |

R1/R2 复用现有 `integration.py --launch` 的端口门控（没有真串口时跳过并标记 SKIP，不伪装通过）。

### 回归

`tio_tests.exe`（289 条）、`tio_window_tests.exe`（67 条）、`tests\window_check.py`（28 条）、`tio_native_probe.exe`（39 条）全绿才算完成。

## 6. 分阶段

| 阶段 | 内容 | 规模 | 可独立验收 |
|---|---|---|---|
| P1 | `session_registry.h` + 写入/删除 + 启动回收 + 诊断 + 窗口提示 | ≈ 250 行 | R1、R2、R3、R4 |
| P2 | 并行停止 + 硬预算 + 结果分类 | ≈ 80 行 | R5 |
| P3 | 嵌套作业实验（`AssignProcessToJobObject`，KILL_ON_JOB_CLOSE） | 探针 | 只出结论，不改产品 |

P1 先合，P2 紧随；两者互不依赖，P2 可以先写测试再改实现。

## 7. 风险与未决

| 风险 | 影响 | 处理 |
|---|---|---|
| pending 认领误杀用户手动起的 tio | 用户终端被关 | 父进程必须是 windowsterminal、创建时间在 `[launchedAt, +120s]`、命令行设备一致、唯一命中；否则不猜 |
| 读不到别的进程命令行（权限/AV） | pending 无法认领 | 保留文件、诊断说明；下次再试；不影响已记录 pid 的常规回收 |
| 回收拖慢第一条查询 | 用户看到「超时」 | 后台线程 + 回收中状态；不在查询路径同步等待 |
| 两个实例互相回收 | 会话被误停 | ownerPid + ownerCreated 存活即跳过 |
| dataDir 未配置/只读 | 写不了登记文件 | 写失败只记诊断，不阻断启动（与 `tio.json` 同策略） |
| 强杀失败 | 端口仍被占 | 标记 failed、保留文件、下次启动再试；不谎报成功 |
| COM 号被别的设备复用 | 停错进程 | 停止前核对 `device` 与命令行末参数 |

未决（需要实测）：

1. Windows Terminal 是否给子进程套了作业对象（决定 P3 可行性）。
2. `NtQueryInformationProcess` 在本机对 wt 子进程是否可用。
3. 强杀插件后 wt 会话的实际存活时长（量出「孤儿窗口期」）。

## 8. 验收清单

- [ ] 强杀插件后重新启动：`sessions\` 为空，COM 口立即可用
- [ ] 两个实例并存：互不回收
- [ ] 伪造 PID / 非 tio 镜像：不动进程
- [ ] shutdown：2 s 内回答，所有会话已有明确结果（exited/forced/gone/failed）
- [ ] 现有四套测试全绿
- [ ] 诊断视图能读出「回收了几个、停了几个、几个留待下次」
