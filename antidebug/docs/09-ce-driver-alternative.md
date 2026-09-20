# 09 —— 备选方案：CE 驱动（dbk64.sys）

| | |
|---|---|
| 状态 | 文档化备选（**不实现**，接口已预留） |
| 日期 | 2026-09-20 |
| 定位 | 与 vtdbg 工程对 cedriver 的处理一致：写成文档备着，不做加载路径 |

## 1. 这是什么、为什么备着

我们默认的内核原语来源是 Intel 网卡驱动 iqvw64e.sys（docs/02 ★B）。它的软肋：
**在微软的"易受攻击驱动阻止列表"里**。虽然研究机可以关掉阻止列表（docs/06 §2），
但在不方便关的场景（比如公司的共用测试机），需要一个"换个梯子"的预案。

Cheat Engine（CE，知名的游戏修改器）自带的内核驱动 dbk64.sys 是另一个常用的
原语来源。vtdbg 工程把它放在 `vt/third_party/cedriver/`（dbk32/64.sys 子模块），
文档定位同样是"BYO 备选，不是加载路径"。

## 2. 两个候选对比

| 维度 | iqvw64e.sys（当前默认） | dbk64.sys（CE，备选） |
|---|---|---|
| 出身 | Intel 网卡驱动（正规签名） | Cheat Engine 自带驱动（带签名发行版可用） |
| 能力面 | 读写内核内存、分配/释放池、调用内核函数（docs/02 §4.1） | 提供自己的读写/分配接口，能力面足够映射一个驱动 |
| 阻止列表 | 在列内，可能被拦 | 同样在安全社区关注范围内，被拦风险同样存在 |
| 设备/接口 | `\Device\Nal`，IOCTL 0x80862007 + case 号复用 | `\Device\CEDRIVER73` 一族，IOCTL 布局不同 |
| 移植工作量 | 已完成（kdm 移植） | 需要新写一个 Provider 适配它的 IOCTL 布局 |
| 许可 | Intel 专有（只用不分发） | CE 遵循自己的许可（LGPL/Creative Commons 混合，需按其官方声明处理） |

## 3. 接入点（如果哪天要做）

架构上已经为此留好了位置，改动**只在一个文件**：

```
loader/src/spi/primitive_provider.hpp     ← 接口不动
loader/src/kdm/intel_nal_provider.cpp     ← 现默认实现
loader/src/kdm/cedriver_provider.cpp      ← 新增这个文件（未来）
```

实现 `IPrimitiveProvider` 的七个方法（Load/Unload/ReadKernel/WriteKernel/
AllocPool/FreePool/CallKernel），把 dbk64 的 IOCTL 映射进去；再在
`driver_loader.cpp` 的工厂函数里加一个选择分支（配置项或环境变量选择 provider）。
上层的映射器、客户端、插件、UI 全部零改动——这正是 docs/02 §3 ★B 存在的意义。

**CallKernel 是难点预警**：iqvw64e 路径的"内核里执行任意函数"靠 NtAddAtom 劫持
实现，这个手法与具体漏洞驱动无关（只要能写内核只读页就行）；dbk64 若提供等效的
写只读内存能力，NtAddAtomInvoker 可原样复用；若没有，需要另找执行原语——
这是评估 dbk64 可行性时的第一个检查项。

## 4. 触发条件（什么时候才值得做）

1. iqvw64e 在目标环境彻底无法加载（阻止列表关不掉 + 无替代机器）；且
2. dbk64 在该环境确认可加载；且
3. 用户明确提出需求。

三者不齐就不动——文档备选的意义是"知道路在哪"，不是"先把路修了"。

## 5. 第三条路：签名加载（一并记录）

如果哪天拿到测试签名资格（`bcdedit /set testsigning on` + 给 antidebug.sys 签名），
可以走完全正规的 SCM 服务加载，那时 `IPrimitiveProvider` 退化为空实现
（内核原语不再需要），入口也从"邮箱协议"换回标准 `DriverEntry`。
这条路对系统防护零侵入，但需要测试签名模式，且驱动出现在服务列表里更显眼。
同样只记录，不实现。
