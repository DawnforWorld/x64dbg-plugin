/* SPDX-License-Identifier: MIT */
/* spi/image_mapper.hpp —— 映射器接口：把驱动 PE 镜像搬进内核内存。
 * 当前唯一实现 KdmapperMapper（kdmapper 移植）：分配内核内存 → 重定位 →
 * 修导入 → 拷贝 → （配合入口调用）执行 CustomDriverEntry。
 *
 * spi/entry_invoker.hpp —— 入口调用接口：在内核里执行驱动入口。
 * 当前唯一实现 NtAddAtomInvoker：把 mov rax,目标; jmp rax 写进
 * nt!NtAddAtom 开头，从用户态调用一次后还原（docs/02 §4.1）。
 *
 * 两者与 IPrimitiveProvider 一起构成"换梯子只动一个实现类"的结构；
 * 现阶段它们由 driver_loader.cpp 直接调用 kdm 的函数完成（见
 * driver_loader.cpp 的注释），接口留作演进点。
 */
#ifndef ADBG_SPI_IMAGE_MAPPER_H_
#define ADBG_SPI_IMAGE_MAPPER_H_

#include <windows.h>

#include <cstdint>

namespace antidebug {
namespace spi {

class IImageMapper {
   public:
    virtual ~IImageMapper() = default;
    /* 返回内核里的镜像基址，0 = 失败。entry_status 带出入口返回值。 */
    virtual uint64_t MapImage(const uint8_t *image, size_t size,
                              uint64_t param1, uint64_t param2,
                              NTSTATUS *entry_status) = 0;
};

class IEntryInvoker {
   public:
    virtual ~IEntryInvoker() = default;
    virtual bool Invoke(uint64_t entry_kva, uint64_t param1, uint64_t param2,
                        uint64_t *out) = 0;
};

}  // namespace spi
}  // namespace antidebug

#endif /* ADBG_SPI_IMAGE_MAPPER_H_ */
