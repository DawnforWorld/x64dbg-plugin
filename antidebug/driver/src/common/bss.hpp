/* SPDX-License-Identifier: MIT */
/* common/bss.hpp —— 手动映射约束的全局变量段控制（docs/02 §6）。
 *
 * 背景：kdmapper 分配的内核内存不保证清零，而它跳过"未初始化数据"
 * 节的拷贝——正常 .bss 里的全局变量拿到的会是垃圾。vtdbg 的做法是把
 * 全部可变全局变量集中放进 .adbss$M 节，入口处用 $A/$Z 锚点之间的
 * 范围防御性清零（entry.cpp）。
 *
 * 用法（仅驱动内 C++ 文件）：
 *   ADBG_BSS_BEGIN
 *   static ULONG g_state = 0;      // 必须零初始化
 *   ADBG_BSS_END
 * 常量表不用套：它们落在 .rdata（有原始数据，映射器会拷贝）。
 */
#ifndef ADBG_BSS_H_
#define ADBG_BSS_H_

#define ADBG_BSS_BEGIN _Pragma("bss_seg(\".adbss$M\")")
#define ADBG_BSS_END _Pragma("bss_seg()")

#endif /* ADBG_BSS_H_ */
