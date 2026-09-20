# 03 —— 接口规范（用户态 ↔ 内核态协议）

| | |
|---|---|
| 状态 | 定稿（协议版本 1） |
| 日期 | 2026-09-20 |
| 规则 | 改这份文档之前不许改 adbg_abi.h；改了协议要同步 bump 版本号并更新这里 |

本文全部结构定义的唯一权威来源是 `antidebug/include/adbg_abi.h`。
两边（用户态/内核态）都只 include 它，不许各自另抄一份。

## 1. 版本与魔数

| 常量 | 值 | 含义 |
|---|---|---|
| `ADBG_ABI_VERSION` | 1 | 协议版本。加载窗口和每个 IOCTL 包都校验 |
| `ADBG_BOOT_SCRATCH_VERSION` | 1 | 启动邮箱结构版本 |
| `ADBG_ENTRY_MAGIC` | `0x416E746444626745` | ASCII "AntdDbgE"，入口参数 2 必须等于它 |
| `ADBG_DRIVER_VERSION` | 1 | 驱动功能版本（QUERY_VERSION 返回） |

**兼容规则**：加字段只能加在结构尾部并 bump 版本；改既有字段的含义 = 新版本号，
旧的直接拒绝（fail loud，宁可失败不可误解）。

## 2. 启动邮箱（加载窗口专用）

加载器在内核内存里放一个 `ADBG_BOOT_SCRATCH`，作为驱动入口和加载器之间
唯一的"交接单"。驱动启动完成后双方就不再用它（会话密钥已取走）。

```c
#pragma pack(push, 8)
typedef struct _ADBG_BOOT_SCRATCH {
    uint32_t Size;                 /* sizeof(struct)，加载器填 */
    uint32_t Version;              /* == ADBG_BOOT_SCRATCH_VERSION */
    uint64_t Magic;                /* == ADBG_ENTRY_MAGIC */
    uint32_t ExpectedAbiVersion;   /* 加载器 -> 驱动：ADBG_ABI_VERSION */
    uint32_t ThreadSpawned;        /* 驱动 -> 加载器：1 = 启动线程已创建 */
    volatile int64_t Result;       /* 终态见下表，轮询到终态即停 */
    int32_t  FailStep;             /* Result==FAILED 时的失败步骤码 */
    uint32_t OsBuildNumber;        /* 驱动 -> 加载器：当前系统版本号 */
    uint8_t  AuthSecret[16];       /* 会话密钥：加载器生成，驱动取走后仍保留供核对 */
    uint64_t DriverBase;           /* 驱动 -> 加载器：镜像基址（调试 .reload 用） */
} ADBG_BOOT_SCRATCH;
#pragma pack(pop)
```

**Result 取值**：

| 值 | 含义 | 是否终态 |
|---|---|---|
| 0 PENDING | 还没开始 | 否 |
| 1 THREAD_ACK | 入口已创建启动线程 | 否 |
| 2 SUCCESS | 全部启动完成，控制设备已就绪 | 是 |
| 3 FAILED | 失败，看 FailStep | 是 |
| 4 STOPPED | 曾成功，后收到 DRIVER_STOP | 是 |

**FailStep 取值**：

| 值 | 步骤 |
|---|---|
| 1 | 入口参数/邮箱头校验失败（不通过邮箱上报，入口直接返回 NTSTATUS） |
| 2 | ABI 版本不匹配 |
| 3 | 全局区清零守卫拒绝（BSS 锚点异常） |
| 4 | 创建启动线程失败 |
| 5 | 解析 10 个内核函数地址失败（日志里有具体哪个） |
| 6 | hook 引擎初始化失败（特征码没扫到，多半是不支持的系统版本） |
| 7 | hook 引擎启动失败 |
| 8 | 创建控制设备失败 |

## 3. 控制设备

| 项 | 值 |
|---|---|
| 设备名 | `\Device\Antidbg` |
| 符号链接 | `\??\AntidbgCtrl`（用户态打开 `\\.\AntidbgCtrl`） |
| 权限 | 管理员和 SYSTEM 完全控制（SDDL `D:P(A;;GA;;;SY)(A;;GA;;;BA)`），普通用户打不开 |
| 并发 | 不排他；antictl 和插件可以同时持有句柄 |

## 4. IOCTL 一览

统一格式：`CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800+n, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)`。

| 码 | 名称 | 方向 | 需要密钥 | 作用 |
|---|---|---|---|---|
| 0x800 | QUERY_VERSION | 出 | 否 | 协议/驱动版本、系统版本、镜像基址 |
| 0x801 | QUERY_STATUS | 出 | 否 | 引擎状态、技术开关、隐藏名单 |
| 0x802 | QUERY_TECHNIQUES | 出 | 否 | 技术清单（给 UI 动态画列表） |
| 0x803 | SESSION_INIT | 入 | 是（本身就是密钥） | 核对密钥；用于"接管"场景 |
| 0x804 | SET_TARGET | 入 | 是 | 增/删/清隐藏名单 |
| 0x805 | SET_TECHNIQUES | 入 | 是 | 设置技术开关位 |
| 0x806 | DRIVER_STOP | 入 | 是 | 停止引擎并删除设备（见 docs/02 §4.3） |

**WOW64 安全规则**：所有结构只用固定宽度类型（`uint32_t/uint64_t/uint8_t[]`），
**不含任何指针**——32 位进程（x32dbg 的插件）发 64 位驱动收，两边看到的
字节布局完全一致。

## 5. 公共包头与各结构

每个请求/响应的第一个字段都是公共包头：

```c
typedef struct _ADBG_IOCTL_HEADER {
    uint32_t Size;      /* sizeof(整个结构) */
    uint32_t Version;   /* == ADBG_ABI_VERSION */
} ADBG_IOCTL_HEADER;
```

驱动侧校验顺序：`InputBufferLength >= Size` → `Version 匹配` →（需要密钥的）
`密钥比对`（常量时间比较，防时序侧信道）→ 业务字段。

```c
/* 0x800 出 */
typedef struct _ADBG_VERSION_INFO {
    ADBG_IOCTL_HEADER Header;
    uint32_t AbiVersion;      /* ADBG_ABI_VERSION */
    uint32_t DriverVersion;   /* ADBG_DRIVER_VERSION */
    uint32_t OsBuildNumber;   /* 如 19045 */
    uint32_t Flags;           /* bit0: 引擎运行中 */
    uint64_t DriverBase;      /* 手动映射基址 */
} ADBG_VERSION_INFO;

/* 0x801 出 */
#define ADBG_MAX_TARGETS 8
typedef struct _ADBG_STATUS_INFO {
    ADBG_IOCTL_HEADER Header;
    uint32_t EngineRunning;      /* 0/1 */
    uint32_t HookedSyscallCount; /* 注册表里解析成功的钩子数 */
    uint32_t TechMask;           /* 当前开关位 */
    uint32_t TargetCount;        /* 隐藏名单进程数 */
    uint32_t TargetPids[ADBG_MAX_TARGETS];
} ADBG_STATUS_INFO;

/* 0x802 出；Count 上限 16 */
typedef struct _ADBG_TECHNIQUE_ENTRY {
    uint32_t TechBit;      /* 1<<n，即 ADBG_TECH_* */
    uint32_t Reserved;
    char     Name[32];     /* ASCII，零结尾 */
} ADBG_TECHNIQUE_ENTRY;
typedef struct _ADBG_TECHNIQUES_INFO {
    ADBG_IOCTL_HEADER Header;
    uint32_t Count;
    uint32_t Reserved;
    ADBG_TECHNIQUE_ENTRY Entries[16];
} ADBG_TECHNIQUES_INFO;

/* 0x803 入 */
typedef struct _ADBG_SESSION_INIT {
    ADBG_IOCTL_HEADER Header;
    uint8_t  Secret[16];
} ADBG_SESSION_INIT;

/* 0x804 入 */
typedef struct _ADBG_SET_TARGET {
    ADBG_IOCTL_HEADER Header;
    uint8_t  Secret[16];
    uint32_t Action;   /* 0=添加 1=移除 2=清空 */
    uint32_t Pid;
} ADBG_SET_TARGET;

/* 0x805 入 */
typedef struct _ADBG_SET_TECHNIQUES {
    ADBG_IOCTL_HEADER Header;
    uint8_t  Secret[16];
    uint32_t TechMask;
    uint32_t Reserved;
} ADBG_SET_TECHNIQUES;

/* 0x806 入 */
typedef struct _ADBG_DRIVER_STOP {
    ADBG_IOCTL_HEADER Header;
    uint8_t  Secret[16];
} ADBG_DRIVER_STOP;
```

**密钥交接**：加载器在加载成功后把 16 字节密钥写到注册表
`HKLM\SOFTWARE\Antidbg` 的 `SessionSecret`（REG_BINARY）；DRIVER_STOP 成功或
加载失败时删除。dp32 插件和新开的 x64dbg 实例从这里读，从而获得控制权。
（该项继承 HKLM\SOFTWARE 的管理员可写、所有用户可读权限——被调试进程通常
非管理员，设备 DACL 已经挡住它；残余风险见 docs/06 §4。）

## 6. 技术位定义（11 项）

| 位 | 常量 | 拦哪个调用 | 改写行为（与 vmp 语义一致） |
|---|---|---|---|
| 1<<0 | `ADBG_TECH_DEBUG_OBJECT` | NtQueryInformationProcess | `ProcessDebugObjectHandle`(0x1E)：直接返回 `STATUS_PORT_NOT_SET`，输出句柄置 NULL——未调试进程就是这表现 |
| 1<<1 | `ADBG_TECH_DEBUG_PORT` | 同上 | `ProcessDebugPort`(7)：结果改 0（没有调试端口） |
| 1<<2 | `ADBG_TECH_DEBUG_FLAGS` | 同上 | `ProcessDebugFlags`(0x1F)：结果改 TRUE（NoDebugInherit） |
| 1<<3 | `ADBG_TECH_HIDE_SET` | NtSetInformationThread | `ThreadHideFromDebugger`：校验句柄后直接返回成功但**不调用**原函数——目标以为藏了线程，调试器照样看得到 |
| 1<<4 | `ADBG_TECH_HIDE_QUERY` | NtQueryInformationThread | `ThreadHideFromDebugger` 查询：结果改 TRUE（配合上一条演戏演全套） |
| 1<<5 | `ADBG_TECH_PROTECTED_CLOSE` | NtClose / NtDuplicateObject | 被调试时：受保护句柄（OBJ_PROTECT_CLOSE）的 Close 返回 `STATUS_HANDLE_NOT_CLOSABLE`，DuplicateObject 剥掉 `DUPLICATE_CLOSE_SOURCE`——把"受保护句柄反调试"的语义原样保住；非法句柄仍返回 `STATUS_INVALID_HANDLE` |
| 1<<6 | `ADBG_TECH_KERNEL_DEBUGGER` | NtQuerySystemInformation | `SystemKernelDebuggerInformation`(0x23)：`{FALSE, TRUE}`；`...Ex`(0x95)：三个字段全 FALSE |
| 1<<7 | `ADBG_TECH_SYSTEM_DEBUG_CONTROL` | NtSystemDebugControl | 除 `SysDbgGetTriageDump`(29) 和 `SysDbgGetLiveKernelDump`(37) 外一律 `STATUS_DEBUGGER_INACTIVE` |
| 1<<8 | `ADBG_TECH_DR_REGISTERS` | NtGet/NtSetContextThread | 把 ContextFlags 里的 `CONTEXT_DEBUG_REGISTERS`(0x10) 位剥掉再调原函数，调完还原；读取时若对方原本要了调试寄存器，则 Dr0-3/Dr6/Dr7 与 LBR 四兄弟（LastBranch/LastException RIP）全清零——硬件断点对目标隐形 |
| 1<<9 | `ADBG_TECH_WOW64_DR_REGISTERS` | NtQuery/NtSetInformationThread | 32 位目标专用：`ThreadWow64Context` 的同款剥离/清零（WOW64_CONTEXT 结构） |
| 1<<10 | `ADBG_TECH_THREAD_CREATE_HIDE` | NtCreateThreadEx | 清掉 CreateFlags 里的 `THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER`(4)——VMProtect 的"对调试器隐身线程"失效 |

`ADBG_TECH_ALL = 0x7FF`；驱动启动默认全开。**总开关 = 隐藏名单**：不在名单里的
进程一切照旧（这是 vmp 没有的最外层闸门）。

## 7. 钩子函数原型

10 个被拦函数的原型定义在 `driver/src/common/nt_extra.hpp`（从 vmp Main.h 移植），
处理函数与原函数签名完全一致（vmp 原样）。唯一新增约定：每个处理函数第一件事
判断 `ExGetPreviousMode() == KernelMode` 则直通原函数——内核自己调用不走改写。

## 8. 插件侧接口

### 8.1 x64dbg 命令（命令栏可输）

| 命令 | 参数 | 作用 |
|---|---|---|
| `AntiDbgLoad` | 无 | 加载并启动驱动（仅 dp64；dp32 提示改用 x64dbg/antictl） |
| `AntiDbgUnload` | 无 | 停止驱动（DRIVER_STOP） |
| `AntiDbgHide` | 无 | 把当前被调试进程 PID 加入隐藏名单 |
| `AntiDbgUnhide` | 无 | 把当前被调试进程 PID 移出隐藏名单 |
| `AntiDbgStatus` | 无 | 把驱动/引擎/名单状态打印到日志窗口 |
| `AntiDbgOptions` | 无 | 打开控制面板对话框 |

### 8.2 菜单

x64dbg 的"插件"菜单下新增子菜单 **AntiDebug**：

```
AntiDebug
├── 控制面板...          ← 打开 UI 对话框
├── 加载并启动
├── 停止
├── ────────
├── 隐藏当前调试进程      ← 勾选态跟随名单
└── 关于
```

### 8.3 事件自动化

| 事件 | 动作（"自动隐藏"开关开启时） |
|---|---|
| CB_CREATEPROCESS / CB_ATTACH | 记下被调试进程 PID |
| CB_SYSTEMBREAKPOINT | 把该 PID 加入隐藏名单 |
| CB_STOPDEBUG | 把该 PID 移出隐藏名单 |

### 8.4 配置持久化

插件目录下 `antidebug_settings.ini`：`auto_hide`（0/1）、`tech_mask`（十六进制）。
不写进 x64dbg 自身的 ini，避免耦合其配置结构。

### 8.5 antictl 命令行

| 命令 | 作用 |
|---|---|
| `antictl load` | 加载并启动驱动 |
| `antictl unload` | 停止驱动 |
| `antictl status` | 查询版本/状态/名单（不需要管理员，若驱动未运行则提示） |
| `antictl hide <pid>` / `unhide <pid>` | 名单增删 |
| `antictl tech` | 查询技术清单和当前开关（`tech 0x2FF` 直接设置） |
| `antictl smoke` | 完整循环：加载→查询→停止，报告每步结果（管理员） |
| `antictl version` | 只打印工具自身与协议版本（离线自测，不碰驱动） |

## 9. 错误处理约定

- 驱动 IOCTL 一律返回 NTSTATUS；结构/版本不对返回 `STATUS_INVALID_PARAMETER`，
  密钥不对返回 `STATUS_ACCESS_DENIED`，长度不够返回 `STATUS_BUFFER_TOO_SMALL`。
- 加载器对"驱动已在运行"返回 `STATUS_ALREADY_REGISTERED`（打 `\\.\AntidbgCtrl`
  能开即认为在运行，直接跳过加载）。
- 任何"无法判断安全"的失败路径（映射后入口没回音、邮箱读取失败）：不回收内核
  内存，报告用户"残留到重启"（docs/02 §4.1 第 5 步的理由）。
