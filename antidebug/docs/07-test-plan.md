# 07 —— 测试计划

| | |
|---|---|
| 状态 | 定稿 |
| 日期 | 2026-09-20 |
| 原则 | 驱动加载类操作**只由用户手动执行**；AI/脚本只做编译与只读自测 |

## 1. 测试分层

| 层 | 内容 | 执行者 | 自动化 |
|---|---|---|---|
| T1 编译 | 全量构建零错误（目标 /W4 零告警） | 开发过程 | 是（cmake 命令） |
| T2 离线自测 | `antictl version/status`：不碰驱动，验证 CLI、ABI 结构尺寸 | 开发过程 | 是 |
| T3 冒烟 | `antictl smoke`：加载→查询→停止 全循环 | **用户（管理员）** | 半自动（单命令） |
| T4 功能 | 逐项技术对照样本验证 | **用户** | 手动（下表） |
| T5 兼容 | 虚拟机过 Win11 路径 | 用户 | 手动 |

## 2. T2：离线自测脚本（AI 可执行）

```bash
build/win-x64-release/antidebug/app/antictl/Release/antictl.exe version
# 期望：打印工具版本 + 协议版本 ADBG_ABI_VERSION=1，退出码 0

build/win-x64-release/antidebug/app/antictl/Release/antictl.exe status
# 期望：提示"驱动未运行（无法打开 \\.\AntidbgCtrl）"，退出码非 0 但不崩溃
```

## 3. T3：冒烟（用户在管理员终端执行）

```bash
antictl.exe smoke
```

按 docs/02 §4.1 的时序走完整循环，每一步都有输出。判定标准：

| 步骤 | 期望输出（要点） |
|---|---|
| 装载 iqvw64e | `NtLoadDriver Status 0x0` |
| 映射驱动 | `image at 0x...`，入口返回 0x0 |
| 邮箱轮询 | `SUCCESS (build=19045, base=0x...)` |
| 会话建立 | `session ok` |
| 查询 | 版本号、引擎 running=1、11 项技术默认全开 |
| 停止 | `stopped`；再次 `antictl status` 应提示未运行 |

失败时对照 FailStep 表（docs/03 §2）。

## 4. T4：逐项技术验证（用户手动）

测试靶子（来自 vmp 工作区，只读使用）：

- `D:\project\vmp\example-vmp\v1-反调试\example.vmp.exe`（开启反调试的加壳样例）
- `D:\project\vmp\example-vmp\v2-无反调试\`（对照组）

流程：x32dbg/x64dbg 打开靶子 → `AntiDbgHide`（或开自动隐藏）→ 能正常断在
入口/单步 = 该轮通过。再逐位关技术（`antictl tech <mask>`）观察哪项关掉后
靶子恢复"发现调试器"，反向确认每项技术的真实作用。

| # | 技术（docs/03 §6 的位） | 开着的现象 | 关掉后预期 |
|---|---|---|---|
| 1 | DEBUG_OBJECT (1<<0) | 程序查调试对象拿不到句柄 | 查到句柄，检测到调试 |
| 2 | DEBUG_PORT (1<<1) | DebugPort=0 | 非零 |
| 3 | DEBUG_FLAGS (1<<2) | DebugFlags=TRUE | FALSE |
| 4 | HIDE_SET (1<<3) | 目标调用 ThreadHideFromDebugger 后调试器仍收到该线程事件 | 该线程事件从此消失 |
| 5 | HIDE_QUERY (1<<4) | 查询返回 TRUE（配合演戏） | 返回真实值 |
| 6 | PROTECTED_CLOSE (1<<5) | NtClose 受保护句柄返回 HANDLE_NOT_CLOSABLE（目标以为"没调试器才这样"） | 程序侧表现与未调试时不一致，检测触发 |
| 7 | KERNEL_DEBUGGER (1<<6) | 内核调试器信息全 FALSE | 真实值 |
| 8 | SYSTEM_DEBUG_CONTROL (1<<7) | 返回 DEBUGGER_INACTIVE | 真实值 |
| 9 | DR_REGISTERS (1<<8) | 目标 GetThreadContext 拿到的 Dr0-7 全 0（看不到硬件断点） | 能看到调试器设的 DR |
| 10 | WOW64_DR (1<<9) | 32 位目标同上 | 同上 |
| 11 | THREAD_CREATE_HIDE (1<<10) | 目标创建的"隐身线程"在调试器里可见 | 线程不可见 |

**边界用例**（必须过）：

- 名单为空时，随便跑个程序调 NtQueryInformationProcess：结果与未装驱动一致
  （总闸门验证）。
- `antictl hide <不存在pid>`：接受但无效果；`unhide` 不存在的 PID 不报错。
- 超过 8 个 PID 再添加：返回错误提示（ADBG_MAX_TARGETS）。
- 密钥错误直接发 SET_TECHNIQUES（改 ABI 结构伪造）：返回 ACCESS_DENIED。
- 结构版本号改错再发：返回 STATUS_INVALID_PARAMETER（fail loud 生效）。

## 5. T5：兼容性验证（可选，按需）

| 环境 | 重点 |
|---|---|
| Win11 虚拟机（22000+） | >18363 引擎路径 + 虚拟机形态（不涉物理机分支） |
| Win11 物理机（若有） | docs/04 §5 的物理机三件套 + 停止后时间是否正常 |
| Win7 虚拟机 | ≤18363 老路径 + GetCpuClock 漂移检测线程 |

## 6. 回归清单（改动后至少跑）

1. T1 + T2（自动化）
2. T3 冒烟（一次）
3. 上表 #1/#4/#9 三项抽测（覆盖"早退型 / 吞调用型 / 前后改写型"三种处理模式）

## 7. 已知不可测项

- 驱动内部错误路径（如 PsCreateSystemThread 失败）无法稳定触发，只做代码走查。
- 睡眠唤醒后的时钟漂移需要长时间睡眠才稳定复现，参考工程已知，本项目不单独建用例。
