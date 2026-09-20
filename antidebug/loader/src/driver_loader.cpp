/* SPDX-License-Identifier: MIT */
/* driver_loader.cpp —— 见同头文件。
 *
 * SPI 说明：IntelNalProvider 的实现是 kdm 静态函数的薄包装；映射与
 * 入口调用（IImageMapper/IEntryInvoker 的职责）当前直接调用
 * kdmapper::MapDriver 完成——它内部按序用到 provider 的全部原语。
 * 换梯子时替换 provider，映射编排不动（docs/02 §3 ★B）。
 */
#include "driver_loader.hpp"

#include <cstdio>
#include <cstdarg>
#include <cstring>

#include "adbg_abi.h"
#include "control_client.hpp"
#include "intel_driver.hpp"
#include "intel_driver_resource.hpp"
#include "kdmapper.hpp"
#include "nt.hpp"
#include "spi/primitive_provider.hpp"

extern "C" BOOLEAN WINAPI SystemFunction036(PVOID RandomBuffer,
                                            ULONG RandomBufferLength);  /* RtlGenRandom */

namespace antidebug {

/* 内嵌的 antidebug.sys 字节（构建期由驱动产物生成） */
#include "antidebug_driver_resource.hpp"

/* ---------------- IntelNalProvider：kdm 静态函数的薄包装 ---------------- */

namespace spi {

NTSTATUS IntelNalProvider::Load() {
    return intel_driver::Load();
}

NTSTATUS IntelNalProvider::Unload() {
    return intel_driver::Unload();
}

bool IntelNalProvider::ReadKernel(uint64_t kva, void *buffer, size_t size) {
    return intel_driver::ReadMemory(kva, buffer, (uint64_t)size) != FALSE;
}

bool IntelNalProvider::WriteKernel(uint64_t kva, const void *buffer,
                                   size_t size) {
    /* kdm 接口是非常量缓冲，这里只是签名适配 */
    return intel_driver::WriteMemory(kva, (void *)buffer, (uint64_t)size) !=
           FALSE;
}

uint64_t IntelNalProvider::AllocPool(size_t size) {
    return intel_driver::AllocatePool(nt::POOL_TYPE::NonPagedPool,
                                      (uint64_t)size);
}

bool IntelNalProvider::FreePool(uint64_t kva) {
    return intel_driver::FreePool(kva) != FALSE;
}

bool IntelNalProvider::CallKernel(uint64_t func, uint64_t a1, uint64_t a2,
                                  uint64_t *out) {
    NTSTATUS status = 0;
    if (!intel_driver::CallKernelFunction(&status, func, a1, a2))
        return false;
    if (out != nullptr)
        *out = (uint64_t)status;
    return true;
}

}  // namespace spi

/* ---------------- 加载窗口 ---------------- */

const char *DriverFailStepName(int32_t step) {
    switch (step) {
        case ADBG_FAIL_ENTRY_ARGS: return "入口参数校验";
        case ADBG_FAIL_ABI_VERSION: return "ABI 版本不匹配";
        case ADBG_FAIL_BSS_ZERO: return "全局区清零守卫";
        case ADBG_FAIL_THREAD_CREATE: return "创建启动线程";
        case ADBG_FAIL_RESOLVE_EXPORTS: return "解析内核函数地址";
        case ADBG_FAIL_ENGINE_INIT: return "引擎初始化(特征码)";
        case ADBG_FAIL_ENGINE_START: return "引擎挂钩";
        case ADBG_FAIL_CONTROL_DEVICE: return "创建控制设备";
        default: return "未知";
    }
}

namespace {

void Fail(DriverLoadResult *result, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    result->error = buf;
}

}  // namespace

DriverLoadResult DriverLoad() {
    DriverLoadResult result;

    /* 0. 已在运行？幂等直接成功 */
    if (ClientDeviceRunning()) {
        result.ok = true;
        result.already_running = true;
        return result;
    }

    /* 1. 装载 iqvw64e（失败时给出最常见的三个原因提示） */
    spi::IntelNalProvider provider;
    NTSTATUS status = provider.Load();
    if (!NT_SUCCESS(status)) {
        Fail(&result, "iqvw64e 装载失败: 0x%08lX", (unsigned long)status);
        if (status == STATUS_UNSUCCESSFUL &&
            intel_driver_resource::driver_size == 0) {
            result.error += "（iqvw64e 资源为空桩：构建时需 -DADBG_IQVW64E_HPP"
                            " 或 _PATH，见 docs/05 §3）";
        } else if (status == STATUS_IMAGE_CERT_REVOKED) {
            result.error += "（易受攻击驱动阻止列表未关闭，见 docs/06 §2）";
        } else if (status == STATUS_ACCESS_DENIED) {
            result.error += "（需要管理员权限，或有杀软拦截）";
        }
        return result;
    }

    /* 2. 启动邮箱：分配、填充、随机密钥、回读校验 */
    const uint64_t scratch_kva = provider.AllocPool(sizeof(ADBG_BOOT_SCRATCH));
    if (scratch_kva == 0) {
        Fail(&result, "启动邮箱分配失败");
        provider.Unload();
        return result;
    }

    ADBG_BOOT_SCRATCH scratch = {};
    scratch.Size = sizeof(scratch);
    scratch.Version = ADBG_BOOT_SCRATCH_VERSION;
    scratch.Magic = ADBG_ENTRY_MAGIC;
    scratch.ExpectedAbiVersion = ADBG_ABI_VERSION;
    if (!SystemFunction036(scratch.AuthSecret, sizeof(scratch.AuthSecret))) {
        Fail(&result, "会话密钥生成失败");
        provider.Unload();
        return result;
    }
    if (!provider.WriteKernel(scratch_kva, &scratch, sizeof(scratch))) {
        Fail(&result, "启动邮箱写入失败");
        provider.Unload();
        return result;
    }
    ADBG_BOOT_SCRATCH verify = {};
    if (!provider.ReadKernel(scratch_kva, &verify, sizeof(verify)) ||
        memcmp(&verify, &scratch, sizeof(scratch)) != 0) {
        Fail(&result, "启动邮箱回读不一致（撕裂写？）");
        provider.Unload();
        return result;
    }

    /* 3. 映射 antidebug.sys（字节来自构建期内嵌） */
    if (antidebug_driver_resource::driver_size == 0) {
        Fail(&result, "antidebug.sys 字节未内嵌（构建异常）");
        provider.Unload();
        return result;
    }
    NTSTATUS entry_status = STATUS_UNSUCCESSFUL;
    const uint64_t base = kdmapper::MapDriver(
        (BYTE *)antidebug_driver_resource::driver, scratch_kva,
        ADBG_ENTRY_MAGIC,
        /*free=*/false, /*destroyHeader=*/true,
        kdmapper::AllocationMode::AllocatePool,
        /*PassAllocationAddressAsFirstParam=*/false, /*callback=*/nullptr,
        &entry_status);
    if (base == 0) {
        Fail(&result, "映射失败（入口返回 0x%08lX）",
             (unsigned long)entry_status);
        /* 邮箱不回收：入口是否已运行无法判定（docs/02 §6） */
        provider.Unload();
        result.error += "；镜像与邮箱留在内核直到重启";
        return result;
    }
    result.image_base = base;

    /* 4. 轮询邮箱到终态（50ms 间隔，10 秒上限） */
    const ULONGLONG deadline = GetTickCount64() + 10000;
    ADBG_BOOT_SCRATCH snap = {};
    for (;;) {
        if (!provider.ReadKernel(scratch_kva, &snap, sizeof(snap))) {
            Fail(&result, "邮箱读取失败（ThreadSpawned=%u）",
                 snap.ThreadSpawned);
            provider.Unload();
            result.error += "；镜像与邮箱留在内核直到重启";
            return result;
        }
        if (snap.Result == ADBG_BOOT_SUCCESS ||
            snap.Result == ADBG_BOOT_FAILED ||
            snap.Result == ADBG_BOOT_STOPPED)
            break;
        if (GetTickCount64() > deadline) {
            Fail(&result, "启动超时（ThreadSpawned=%u, Result=%lld）",
                 snap.ThreadSpawned, (long long)snap.Result);
            provider.Unload();
            result.error += "；镜像与邮箱留在内核直到重启";
            return result;
        }
        Sleep(50);
    }

    if (snap.Result != ADBG_BOOT_SUCCESS) {
        Fail(&result, "启动失败于步骤 %d（%s）", (int)snap.FailStep,
             DriverFailStepName(snap.FailStep));
        provider.Unload();
        result.error += "；镜像与邮箱留在内核直到重启";
        return result;
    }

    /* 5. 成功：取走密钥，回收邮箱（驱动已在 SUCCESS 前完成拷贝），
     *    卸载 iqvw64e（覆写并删除临时文件） */
    memcpy(result.secret, snap.AuthSecret, 16);
    provider.FreePool(scratch_kva);
    provider.Unload();

    /* 6. 会话建立 + 密钥交接注册表 */
    HANDLE device = ClientOpenDevice();
    if (device == INVALID_HANDLE_VALUE) {
        Fail(&result, "映射成功但打不开控制设备（步骤8异常？）");
        return result;
    }
    NTSTATUS ioctl_status = 0;
    const bool session_ok =
        ClientSessionInit(device, result.secret, &ioctl_status);
    if (session_ok) {
        ADBG_VERSION_INFO version = {};
        ClientQueryVersion(device, &version, nullptr);
        result.os_build = version.OsBuildNumber;
    }
    ClientCloseDevice(device);
    if (!session_ok) {
        Fail(&result, "SESSION_INIT 失败: 0x%08lX",
             (unsigned long)ioctl_status);
        return result;
    }
    if (!ClientSaveSecretToRegistry(result.secret)) {
        Fail(&result, "密钥写入注册表失败（权限？）");
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace antidebug
