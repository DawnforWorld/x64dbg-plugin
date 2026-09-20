# 08 —— vmp 移植对照表

| | |
|---|---|
| 状态 | 定稿 |
| 日期 | 2026-09-20 |
| 源 | `D:\project\vmp\vmp`（11 个文件，约 2490 行） |

## 1. 文件级对照

| vmp 源文件 | 行数 | 去向 | 说明 |
|---|---|---|---|
| `Main.cpp` 的 10 个 `Hk*` 处理函数（401-1015 行） | ~615 | `driver/src/techniques/` 按类拆成 8 个文件 | 语义原样；公共样板（前后模式判断、`__try` 包裹）收敛成小工具函数 |
| `Main.cpp` 的 `InfHookCallback`（234-277 行） | 44 | `driver/src/framework/registry.cpp` 的派发函数 | if/else 连锁 → 表驱动 |
| `Main.cpp` 的 `DriverEntry`（279-371 行） | 93 | 重写为 `driver/src/entry.cpp`（邮箱协议） | 入口形态整个换掉（手动映射） |
| `Main.cpp` 的 ntdll SSDT 索引解析（22-232 行） | ~210 | **不移植** | 死代码：vmp 里只算不用于派发（按地址派发），报告里明确记录 |
| `Main.h` | 273 | 拆分：类型 → `common/nt_extra.hpp`；钩子声明 → 各 technique 文件 | WOW64_CONTEXT、SYSDBG_COMMAND 等原样 |
| `hook.cpp` / `hook.hpp`（vmp 的引擎拷贝） | 405 | **不用 vmp 版**，改用 InfinityHookPro 的引擎（更新、覆盖 24H2、修了物理机问题） | 见 §3 |
| `utils.hpp` | 168 | `driver/src/platform/utils.*` | 模块枚举/特征码/KVAS 解析，逻辑原样、规范重写 |
| `imports.hpp` | 45 | 并入 `platform/utils.*` | ZwQuerySystemInformation/NtTraceControl 声明 |
| `headers.hpp` | 10 | 并入各文件的 include 区 | — |
| `hde/`（HDE64 反汇编引擎） | 572 | 随引擎走：InfinityHookPro 自带一份，移植它的 | 第三方，保留原版权 |
| `IsCurrentProcessTargetProcess`（382 行，stub） | 3 | **真正实现**为 `framework/policy.cpp` 的按 PID 判定 | vmp 留了占位没接线 |
| `GetProcessIDFromThreadHandle`（388 行） | 12 | 不移植 | 无调用者 |

## 2. 处理函数语义保留清单（验收用）

移植时这些常量和行为**一个都不能变**（对照 vmp Main.cpp 核对）：

| 处理函数 | 必须保留的语义 |
|---|---|
| HkNtQueryInformationProcess | `0x1E` 早退返回 `STATUS_PORT_NOT_SET`、写 NULL 句柄和 `sizeof(HANDLE)`；`ProcessDebugFlags`→TRUE；`ProcessDebugPort`→0；`ProcessBasicInformation` 明确不改；KernelMode 直通 |
| HkNtSetInformationThread | `ThreadHideFromDebugger` 且长度 0：`ObReferenceObjectByHandle(THREAD_SET_INFORMATION)` 校验后**直接返回、不调原函数**；`ThreadWow64Context`：调原函数前把 ContextFlags 的 0x10 位剥掉、调后还原 |
| HkFnNtClose | 完全重实现：`ObQueryObjectAuditingByHandle` 判非法句柄→`STATUS_INVALID_HANDLE`；被调试且句柄带 `OBJ_PROTECT_CLOSE`→`STATUS_HANDLE_NOT_CLOSABLE`；否则 `ObCloseHandle` |
| HkNtQuerySystemInformation | 0x23→`{FALSE,TRUE}`；0x95→三 FALSE；ReturnLength 保持原值（先读后写回） |
| HkNtQueryInformationThread | ThreadWow64Context：剥 0x10 调原函数，若对方要了 Dr 则清 Wow64 的 Dr0-3/6/7；ThreadHideFromDebugger 查询→TRUE |
| HkNtGetContextThread | 剥 0x10→调→还原 flags；要了 Dr 则 Dr0-3/Dr6/Dr7 + LastBranch/LastException 四个 RIP 全清零（`_WIN64` 下） |
| HkNtSetContextThread | 剥 0x10→调→还原 |
| HkNtSystemDebugControl | 只放行 `SysDbgGetTriageDump`(29)、`SysDbgGetLiveKernelDump`(37)，其余 `STATUS_DEBUGGER_INACTIVE` |
| HkNtDuplicateObject | 被调试且源句柄带 `OBJ_PROTECT_CLOSE` 且带 `DUPLICATE_CLOSE_SOURCE`：剥掉该 flag 再调原函数 |
| HkNtCreateThreadEx | 清 `THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER`(0x4) 再调原函数 |

所有用户内存写入保持 `__try + ProbeForWrite` 防护模式（这是 SEH 的正当用途，
CONTRIBUTING.md 有说明）。

## 3. 引擎为什么不用 vmp 那份

vmp 的 `hook.cpp` 是 InfinityHook 的**早期单文件拷贝**，InfinityHookPro 参考克隆
是原作者后续维护的版本。差异（已逐行比对）：

| 差异点 | vmp 版 | InfinityHookPro 版 |
|---|---|---|
| GetCpuClock 偏移 | 自己一套分支（≥19045 用 0x20、22000 用 0x18、默认 0x28 + MmIsAddressValid 兜底翻转） | 收敛为"≤7601 或 ≥22000 → 0x18，其余 0x28"（实测口径，docs/04 §3） |
| 物理机支持 | 没有 | 有（假性能计数器 + 类型切换 + QpcBias 修正，docs/04 §5） |
| Win11 23606+ 栈特征码 | 只认 0x501802 | 501802/601802 都认 |
| 停止还原 | 旧路径不还原 GetCpuClock（泄漏） | 两条路径都完整还原 |

结论：引擎层整体移植 InfinityHookPro，vmp 的 hook.cpp 仅作理解参考。

## 4. 明确修掉的 vmp 缺陷（本项目行为）

| vmp 缺陷 | 本项目行为 |
|---|---|
| 无按进程过滤（对所有用户进程生效） | 策略层 PID 名单，名单外零改写 |
| DriverEntry 只校验 5/10 个函数地址就启动 | 注册表初始化全量 fail-loud（FailStep=5 报出缺哪个） |
| 热路径里 DbgPrintEx 无条件打印 | 日志宏编译期开关（`ADBG_DBG`），默认关 |
| 加一个钩子要改 5 处 | 加 1 个表项 + 1 个文件 |
| 无法停止/无控制面 | IOCTL 控制面（docs/03） |

## 5. 不移植清单（死代码）

- `InitializeNtdllSsdt` / `GetExportSsdtIndex` / `RvaToOffset` / `GetExportOffset`
  （约 210 行）：解析盘上 ntdll.dll 算 SSDT 索引，算完只打日志。
  派发按地址（`MmGetSystemRoutineAddress`），索引用不上。将来若做索引派发再回收。
- `DebugTargetPrefix[] = "target"`：进程名前缀过滤的占位，从未接线。
  我们的替代物是 PID 名单（功能更贴合"当前调试的进程"这个真实需求）。
- `GetProcessIDFromThreadHandle`：无调用者。
