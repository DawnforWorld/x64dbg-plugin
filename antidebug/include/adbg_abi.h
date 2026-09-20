/* SPDX-License-Identifier: MIT */
/* adbg_abi.h —— 用户态与内核态之间唯一的共享协议头文件。
 *
 * 本文件是 docs/03-api-spec.md 的权威实现：两边（loader/plugin/antictl 与
 * antidebug.sys）都只 include 它，任何一边不许私下再定义协议结构。
 *
 * 约束：
 *   - 纯 C、只用固定宽度类型、不含指针 —— 32 位进程发 64 位驱动收，
 *     字节布局必须完全一致（WOW64 安全，docs/03 §4）。
 *   - 兼容规则：加字段只能加结构尾部并 bump ADBG_ABI_VERSION；
 *     改既有字段含义 = 新版本号，旧版直接拒绝。
 */
#ifndef ADBG_ABI_H_
#define ADBG_ABI_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 版本与魔数                                                          */
/* ------------------------------------------------------------------ */

#define ADBG_ABI_VERSION 1u
#define ADBG_BOOT_SCRATCH_VERSION 1u
#define ADBG_DRIVER_VERSION 1u

/* 入口参数 2 必须等于它（ASCII "AntdDbgE"） */
#define ADBG_ENTRY_MAGIC 0x416E746444626745ull

/* 设备名：内核 \Device\Antidbg，用户态打开 \\.\AntidbgCtrl */
#define ADBG_DEVICE_NAME L"\\Device\\Antidbg"
#define ADBG_SYMLINK_NAME L"\\??\\AntidbgCtrl"
#define ADBG_USER_DEVICE_PATH "\\\\.\\AntidbgCtrl"

/* 会话密钥的注册表交接位置（docs/03 §5） */
#define ADBG_SECRET_REG_KEY L"SOFTWARE\\Antidbg"
#define ADBG_SECRET_REG_VALUE L"SessionSecret"

/* ------------------------------------------------------------------ */
/* 启动邮箱（加载窗口专用，docs/03 §2）                                 */
/* ------------------------------------------------------------------ */

/* Result 终态 */
enum {
    ADBG_BOOT_PENDING = 0,
    ADBG_BOOT_THREAD_ACK = 1,   /* 非终态：入口已创建启动线程 */
    ADBG_BOOT_SUCCESS = 2,
    ADBG_BOOT_FAILED = 3,
    ADBG_BOOT_STOPPED = 4,      /* 曾成功，后收到 DRIVER_STOP */
};

/* FailStep 步骤码（Result==FAILED 时有意义） */
enum {
    ADBG_FAIL_ENTRY_ARGS = 1,       /* 不通过邮箱上报：入口直接返回 NTSTATUS */
    ADBG_FAIL_ABI_VERSION = 2,
    ADBG_FAIL_BSS_ZERO = 3,
    ADBG_FAIL_THREAD_CREATE = 4,
    ADBG_FAIL_RESOLVE_EXPORTS = 5,  /* 解析 10 个内核函数地址失败 */
    ADBG_FAIL_ENGINE_INIT = 6,      /* 特征码没扫到（多半是不支持的系统版本） */
    ADBG_FAIL_ENGINE_START = 7,
    ADBG_FAIL_CONTROL_DEVICE = 8,
};

#pragma pack(push, 8)
typedef struct _ADBG_BOOT_SCRATCH {
    uint32_t Size;                /* sizeof(struct)，加载器填 */
    uint32_t Version;             /* == ADBG_BOOT_SCRATCH_VERSION */
    uint64_t Magic;               /* == ADBG_ENTRY_MAGIC */
    uint32_t ExpectedAbiVersion;  /* 加载器 -> 驱动：ADBG_ABI_VERSION */
    uint32_t ThreadSpawned;       /* 驱动 -> 加载器：1 = 启动线程已创建 */
    volatile int64_t Result;      /* ADBG_BOOT_*，轮询到终态即停 */
    int32_t FailStep;             /* ADBG_FAIL_* */
    uint32_t OsBuildNumber;       /* 驱动 -> 加载器 */
    uint8_t AuthSecret[16];       /* 会话密钥：加载器生成 */
    uint64_t DriverBase;          /* 加载器填：镜像基址（kd .reload 用） */
} ADBG_BOOT_SCRATCH;
#pragma pack(pop)

/* ------------------------------------------------------------------ */
/* 技术位（docs/03 §6，共 11 项）                                       */
/* ------------------------------------------------------------------ */

#define ADBG_TECH_DEBUG_OBJECT (1u << 0)
#define ADBG_TECH_DEBUG_PORT (1u << 1)
#define ADBG_TECH_DEBUG_FLAGS (1u << 2)
#define ADBG_TECH_HIDE_SET (1u << 3)
#define ADBG_TECH_HIDE_QUERY (1u << 4)
#define ADBG_TECH_PROTECTED_CLOSE (1u << 5)
#define ADBG_TECH_KERNEL_DEBUGGER (1u << 6)
#define ADBG_TECH_SYSTEM_DEBUG_CONTROL (1u << 7)
#define ADBG_TECH_DR_REGISTERS (1u << 8)
#define ADBG_TECH_WOW64_DR_REGISTERS (1u << 9)
#define ADBG_TECH_THREAD_CREATE_HIDE (1u << 10)

#define ADBG_TECH_ALL 0x7FFu
#define ADBG_TECH_COUNT 11u
#define ADBG_MAX_TARGETS 8u

/* ------------------------------------------------------------------ */
/* IOCTL 码（docs/03 §4）                                              */
/* ------------------------------------------------------------------ */

#ifndef CTL_CODE
/* 内核侧由 wdm.h 提供；这里给用户态一个等价定义
 * （FILE_DEVICE_UNKNOWN=0x22，access=FILE_READ_DATA|FILE_WRITE_DATA=3，
 *  METHOD_BUFFERED=0，function 位左移 2） */
#define ADBG_CTL_CODE(Function) \
    ((uint32_t)(0x22u << 16) | (uint32_t)(3u << 14) | (uint32_t)(Function) << 2)
#else
#define ADBG_CTL_CODE(Function) \
    ((uint32_t)CTL_CODE(FILE_DEVICE_UNKNOWN, Function, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA))
#endif

#define ADBG_IOCTL_QUERY_VERSION ADBG_CTL_CODE(0x800)
#define ADBG_IOCTL_QUERY_STATUS ADBG_CTL_CODE(0x801)
#define ADBG_IOCTL_QUERY_TECHNIQUES ADBG_CTL_CODE(0x802)
#define ADBG_IOCTL_SESSION_INIT ADBG_CTL_CODE(0x803)
#define ADBG_IOCTL_SET_TARGET ADBG_CTL_CODE(0x804)
#define ADBG_IOCTL_SET_TECHNIQUES ADBG_CTL_CODE(0x805)
#define ADBG_IOCTL_DRIVER_STOP ADBG_CTL_CODE(0x806)

/* ------------------------------------------------------------------ */
/* IOCTL 收发结构（docs/03 §5）                                         */
/* ------------------------------------------------------------------ */

typedef struct _ADBG_IOCTL_HEADER {
    uint32_t Size;
    uint32_t Version;
} ADBG_IOCTL_HEADER;

typedef struct _ADBG_VERSION_INFO {
    ADBG_IOCTL_HEADER Header;
    uint32_t AbiVersion;
    uint32_t DriverVersion;
    uint32_t OsBuildNumber;
    uint32_t Flags;      /* bit0: 引擎运行中 */
    uint64_t DriverBase;
} ADBG_VERSION_INFO;

#define ADBG_VERSION_FLAG_ENGINE_RUNNING 0x1u

typedef struct _ADBG_STATUS_INFO {
    ADBG_IOCTL_HEADER Header;
    uint32_t EngineRunning;
    uint32_t HookedSyscallCount;
    uint32_t TechMask;
    uint32_t TargetCount;
    uint32_t TargetPids[ADBG_MAX_TARGETS];
} ADBG_STATUS_INFO;

#define ADBG_TECH_MAX_ENTRIES 16u

typedef struct _ADBG_TECHNIQUE_ENTRY {
    uint32_t TechBit;
    uint32_t Reserved;
    char Name[32];       /* ASCII，零结尾 */
} ADBG_TECHNIQUE_ENTRY;

typedef struct _ADBG_TECHNIQUES_INFO {
    ADBG_IOCTL_HEADER Header;
    uint32_t Count;
    uint32_t Reserved;
    ADBG_TECHNIQUE_ENTRY Entries[ADBG_TECH_MAX_ENTRIES];
} ADBG_TECHNIQUES_INFO;

typedef struct _ADBG_SESSION_INIT {
    ADBG_IOCTL_HEADER Header;
    uint8_t Secret[16];
} ADBG_SESSION_INIT;

/* SET_TARGET 的 Action 取值 */
enum {
    ADBG_TARGET_ADD = 0,
    ADBG_TARGET_REMOVE = 1,
    ADBG_TARGET_CLEAR = 2,
};

typedef struct _ADBG_SET_TARGET {
    ADBG_IOCTL_HEADER Header;
    uint8_t Secret[16];
    uint32_t Action;
    uint32_t Pid;
} ADBG_SET_TARGET;

typedef struct _ADBG_SET_TECHNIQUES {
    ADBG_IOCTL_HEADER Header;
    uint8_t Secret[16];
    uint32_t TechMask;
    uint32_t Reserved;
} ADBG_SET_TECHNIQUES;

typedef struct _ADBG_DRIVER_STOP {
    ADBG_IOCTL_HEADER Header;
    uint8_t Secret[16];
} ADBG_DRIVER_STOP;

/* ------------------------------------------------------------------ */
/* 技术位 <-> 名称（QUERY_TECHNIQUES 用，两边共用）                      */
/* ------------------------------------------------------------------ */

typedef struct _ADBG_TECH_NAME {
    uint32_t TechBit;
    const char *Name;
} ADBG_TECH_NAME;

/* 定义 ADBG_DECLARE_TECH_NAMES 后获得表体（避免头文件重复定义） */
#ifdef ADBG_DECLARE_TECH_NAMES
static const ADBG_TECH_NAME kAdbgTechNames[ADBG_TECH_COUNT] = {
    {ADBG_TECH_DEBUG_OBJECT, "ProcessDebugObjectHandle"},
    {ADBG_TECH_DEBUG_PORT, "ProcessDebugPort"},
    {ADBG_TECH_DEBUG_FLAGS, "ProcessDebugFlags"},
    {ADBG_TECH_HIDE_SET, "ThreadHideFromDebugger(set)"},
    {ADBG_TECH_HIDE_QUERY, "ThreadHideFromDebugger(query)"},
    {ADBG_TECH_PROTECTED_CLOSE, "ProtectedHandle(NtClose/Dup)"},
    {ADBG_TECH_KERNEL_DEBUGGER, "KernelDebuggerInformation"},
    {ADBG_TECH_SYSTEM_DEBUG_CONTROL, "NtSystemDebugControl"},
    {ADBG_TECH_DR_REGISTERS, "DrRegisters(x64)"},
    {ADBG_TECH_WOW64_DR_REGISTERS, "DrRegisters(WOW64)"},
    {ADBG_TECH_THREAD_CREATE_HIDE, "ThreadCreateHideFlag"},
};
#endif

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* ADBG_ABI_H_ */
