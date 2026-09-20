/* SPDX-License-Identifier: MIT */
/* spi/primitive_provider.hpp —— ★B 可换接口：内核原语提供者（docs/02 §3）。
 *
 * 一个"原语提供者"= 一条能把无签名驱动送进内核运行的路。默认实现
 * IntelNalProvider 包装 kdm 移植代码（iqvw64e.sys，vtdbg 同款）。
 * 备选：CE dbk64（docs/09，仅文档化）。
 *
 * 映射器（IImageMapper）与入口调用（IEntryInvoker）配套声明在
 * image_mapper.hpp / entry_invoker.hpp，当前由同一份 kdmapper 移植
 * 代码实现（KdmapperMapper / NtAddAtomInvoker）。
 */
#ifndef ADBG_SPI_PRIMITIVE_PROVIDER_H_
#define ADBG_SPI_PRIMITIVE_PROVIDER_H_

#include <windows.h>

#include <cstdint>

namespace antidebug {
namespace spi {

class IPrimitiveProvider {
   public:
    virtual ~IPrimitiveProvider() = default;

    /* 装载/卸载"梯子"驱动（iqvw64e：落盘+注册表+NtLoadDriver+\\.\Nal） */
    virtual NTSTATUS Load() = 0;
    virtual NTSTATUS Unload() = 0;

    virtual bool ReadKernel(uint64_t kva, void *buffer, size_t size) = 0;
    virtual bool WriteKernel(uint64_t kva, const void *buffer, size_t size) = 0;
    virtual uint64_t AllocPool(size_t size) = 0;  /* NonPagedPool */
    virtual bool FreePool(uint64_t kva) = 0;
    /* 在内核里执行指定函数（NtAddAtom 劫持实现） */
    virtual bool CallKernel(uint64_t func, uint64_t a1, uint64_t a2,
                            uint64_t *out) = 0;
};

/* 默认实现：包装 loader/src/kdm 的静态函数 */
class IntelNalProvider : public IPrimitiveProvider {
   public:
    NTSTATUS Load() override;
    NTSTATUS Unload() override;
    bool ReadKernel(uint64_t kva, void *buffer, size_t size) override;
    bool WriteKernel(uint64_t kva, const void *buffer, size_t size) override;
    uint64_t AllocPool(size_t size) override;
    bool FreePool(uint64_t kva) override;
    bool CallKernel(uint64_t func, uint64_t a1, uint64_t a2,
                    uint64_t *out) override;
};

}  // namespace spi
}  // namespace antidebug

#endif /* ADBG_SPI_PRIMITIVE_PROVIDER_H_ */
