/* SPDX-License-Identifier: MIT */
/* control/device.cpp —— 见同头文件。IOCTL 语义见 docs/03 §4-§6。 */
#define ADBG_DECLARE_TECH_NAMES
#include "adbg_abi.h"
#undef ADBG_DECLARE_TECH_NAMES

#include "control/device.hpp"

#include "common/bss.hpp"
#include "engine/hook_engine.hpp"
#include "framework/policy.hpp"
#include "framework/registry.hpp"
#include "platform/utils.hpp"

/* wdm.h 没有声明：映射驱动自建驱动对象用的未公开导出 */
extern "C" NTKERNELAPI NTSTATUS NTAPI IoCreateDriver(
    PCUNICODE_STRING DriverName,
    NTSTATUS(NTAPI *InitializationFunction)(PDRIVER_OBJECT, PUNICODE_STRING));

namespace {

ADBG_BSS_BEGIN
static uint8_t g_secret[16] = {0};
static PDEVICE_OBJECT g_device = nullptr;
static PDRIVER_OBJECT g_driver = nullptr;
static volatile LONG g_stopped = 0;
ADBG_BSS_END

/* 设备类 GUID（自造，仅 IoCreateDeviceSecure 需要） */
const GUID kDeviceClassGuid = {0x7a3c5e81, 0x46b2, 0x4d9c,
                               {0x8f, 0x1e, 0xc2, 0x55, 0x60, 0x19, 0x8a,
                                0x74}};

/* 管理员 DACL：SYSTEM 与 Administrators 完全控制，普通用户打不开 */
const wchar_t kSddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";

/* 常量时间比较：不因比较对象不同提前退出（防时序侧信道） */
bool SecretEqual(const uint8_t *a, const uint8_t *b) {
    uint8_t diff = 0;
    for (ULONG i = 0; i < 16; ++i)
        diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

bool HeaderOk(const ADBG_IOCTL_HEADER *header, uint32_t input_length,
              uint32_t expected_size) {
    if (header == nullptr || input_length < sizeof(ADBG_IOCTL_HEADER))
        return false;
    if (header->Version != ADBG_ABI_VERSION)
        return false;
    if (header->Size != expected_size || input_length < expected_size)
        return false;
    return true;
}

/* ---------------- 停止路径（docs/02 §4.3） ---------------- */

NTSTATUS DoStop(const uint8_t *secret) {
    if (!SecretEqual(secret, g_secret))
        return STATUS_ACCESS_DENIED;

    NTSTATUS status = adbg_engine::InfinityHookProEngine.Stop();
    AdbgPolicySetTarget(ADBG_TARGET_CLEAR, 0);

    UNICODE_STRING link;
    RtlInitUnicodeString(&link, ADBG_SYMLINK_NAME);
    IoDeleteSymbolicLink(&link);

    /* IoDeleteDevice 的语义是"标记删除，引用归零后真正消失"——在
     * 自己的分发例程里调用是安全的 */
    if (g_device != nullptr) {
        IoDeleteDevice(g_device);
        g_device = nullptr;
    }
    g_driver = nullptr;  /* 驱动对象留在系统里直到重启（与镜像同寿命） */

    InterlockedExchange(&g_stopped, 1);
    return status;
}

/* ---------------- IRP 分发 ---------------- */

NTSTATUS DispatchCreateClose(PDEVICE_OBJECT device, PIRP irp) {
    UNREFERENCED_PARAMETER(device);
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS DispatchControl(PDEVICE_OBJECT device, PIRP irp) {
    UNREFERENCED_PARAMETER(device);

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    const ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    const ULONG in_len = stack->Parameters.DeviceIoControl.InputBufferLength;
    const ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    /* METHOD_BUFFERED：进出共用一个系统缓冲区 */
    void *buffer = irp->AssociatedIrp.SystemBuffer;

    NTSTATUS status = STATUS_INVALID_PARAMETER;
    ULONG information = 0;

    if (buffer == nullptr && in_len == 0 && out_len == 0) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }

    switch (code) {
    case ADBG_IOCTL_QUERY_VERSION: {
        if (out_len < sizeof(ADBG_VERSION_INFO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        auto *out = (ADBG_VERSION_INFO *)buffer;
        out->Header.Size = sizeof(ADBG_VERSION_INFO);
        out->Header.Version = ADBG_ABI_VERSION;
        out->AbiVersion = ADBG_ABI_VERSION;
        out->DriverVersion = ADBG_DRIVER_VERSION;
        out->OsBuildNumber = adbg_plat::GetSystemBuildNumber();
        out->Flags = adbg_engine::InfinityHookProEngine.IsRunning()
                         ? ADBG_VERSION_FLAG_ENGINE_RUNNING
                         : 0;
        if (g_stopped != 0)
            out->Flags = 0;
        out->DriverBase = 0;  /* 基址由加载器自己知道（docs/03 §5） */
        information = sizeof(ADBG_VERSION_INFO);
        status = STATUS_SUCCESS;
        break;
    }

    case ADBG_IOCTL_QUERY_STATUS: {
        if (out_len < sizeof(ADBG_STATUS_INFO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        auto *out = (ADBG_STATUS_INFO *)buffer;
        out->Header.Size = sizeof(ADBG_STATUS_INFO);
        out->Header.Version = ADBG_ABI_VERSION;
        out->EngineRunning =
            (g_stopped == 0 && adbg_engine::InfinityHookProEngine.IsRunning())
                ? 1u
                : 0u;
        out->HookedSyscallCount = AdbgRegistryHookedCount();
        out->TechMask = AdbgPolicyGetMask();
        out->TargetCount = AdbgPolicyGetTargets(out->TargetPids);
        information = sizeof(ADBG_STATUS_INFO);
        status = STATUS_SUCCESS;
        break;
    }

    case ADBG_IOCTL_QUERY_TECHNIQUES: {
        if (out_len < sizeof(ADBG_TECHNIQUES_INFO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        auto *out = (ADBG_TECHNIQUES_INFO *)buffer;
        out->Header.Size = sizeof(ADBG_TECHNIQUES_INFO);
        out->Header.Version = ADBG_ABI_VERSION;
        out->Count = ADBG_TECH_COUNT;
        out->Reserved = 0;
        for (ULONG i = 0; i < ADBG_TECH_COUNT; ++i) {
            out->Entries[i].TechBit = kAdbgTechNames[i].TechBit;
            out->Entries[i].Reserved = 0;
            RtlZeroMemory(out->Entries[i].Name, sizeof(out->Entries[i].Name));
            ULONG j = 0;
            for (; kAdbgTechNames[i].Name[j] != '\0' && j < 31; ++j)
                out->Entries[i].Name[j] = kAdbgTechNames[i].Name[j];
        }
        information = sizeof(ADBG_TECHNIQUES_INFO);
        status = STATUS_SUCCESS;
        break;
    }

    case ADBG_IOCTL_SESSION_INIT: {
        const auto *in = (const ADBG_SESSION_INIT *)buffer;
        if (!HeaderOk(&in->Header, in_len, sizeof(ADBG_SESSION_INIT)))
            break;
        status = SecretEqual(in->Secret, g_secret) ? STATUS_SUCCESS
                                                   : STATUS_ACCESS_DENIED;
        break;
    }

    case ADBG_IOCTL_SET_TARGET: {
        const auto *in = (const ADBG_SET_TARGET *)buffer;
        if (!HeaderOk(&in->Header, in_len, sizeof(ADBG_SET_TARGET)))
            break;
        if (!SecretEqual(in->Secret, g_secret)) {
            status = STATUS_ACCESS_DENIED;
            break;
        }
        status = AdbgPolicySetTarget(in->Action, in->Pid);
        break;
    }

    case ADBG_IOCTL_SET_TECHNIQUES: {
        const auto *in = (const ADBG_SET_TECHNIQUES *)buffer;
        if (!HeaderOk(&in->Header, in_len, sizeof(ADBG_SET_TECHNIQUES)))
            break;
        if (!SecretEqual(in->Secret, g_secret)) {
            status = STATUS_ACCESS_DENIED;
            break;
        }
        AdbgPolicySetMask(in->TechMask);
        status = STATUS_SUCCESS;
        break;
    }

    case ADBG_IOCTL_DRIVER_STOP: {
        const auto *in = (const ADBG_DRIVER_STOP *)buffer;
        if (!HeaderOk(&in->Header, in_len, sizeof(ADBG_DRIVER_STOP)))
            break;
        if (g_stopped != 0) {
            status = STATUS_DEVICE_NOT_CONNECTED;  /* 已经停了 */
            break;
        }
        status = DoStop(in->Secret);
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

complete:
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

/* IoCreateDriver 的初始化回调：真正创建设备 */
NTSTATUS DriverInitialize(PDRIVER_OBJECT driver_object, PUNICODE_STRING) {
    UNICODE_STRING device_name;
    RtlInitUnicodeString(&device_name, ADBG_DEVICE_NAME);

    UNICODE_STRING sddl;
    RtlInitUnicodeString(&sddl, kSddl);

    PDEVICE_OBJECT device = nullptr;
    NTSTATUS status = IoCreateDeviceSecure(
        driver_object, 0, &device_name, FILE_DEVICE_UNKNOWN, 0, FALSE, &sddl,
        &kDeviceClassGuid, &device);
    if (!NT_SUCCESS(status))
        return status;

    UNICODE_STRING link;
    RtlInitUnicodeString(&link, ADBG_SYMLINK_NAME);
    status = IoCreateSymbolicLink(&link, &device_name);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(device);
        return status;
    }

    driver_object->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
    driver_object->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
    driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchControl;

    device->Flags &= ~DO_DEVICE_INITIALIZING;

    g_driver = driver_object;
    g_device = device;
    return STATUS_SUCCESS;
}

}  // namespace

extern "C" BOOLEAN AdbgControlIsStopped(void) {
    return g_stopped != 0 ? TRUE : FALSE;
}

extern "C" NTSTATUS AdbgControlCreate(const uint8_t *secret) {
    if (secret == nullptr)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(g_secret, secret, sizeof(g_secret));

    UNICODE_STRING driver_name;
    RtlInitUnicodeString(&driver_name, L"\\Driver\\Antidbg");
    return IoCreateDriver(&driver_name, DriverInitialize);
}
