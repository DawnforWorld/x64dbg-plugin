# 02 —— 架构

| | |
|---|---|
| 状态 | 定稿 |
| 日期 | 2026-09-20 |
| 读者 | 改代码前必读 |

## 1. 一句话设计思想

**把"最容易变"的三样东西各锁在一个接口后面（hook 引擎、加载原语、控制传输），
把"最稳定"的业务语义（10 个钩子的处理逻辑）放在中间。**
这样无论 Windows 怎么升级、加载手法怎么换、技术怎么加，变化都只落在自己那一层，
不会穿透整个工程。

## 2. 分层总图

```
用户态 ────────────────────────────────────────────────────────────────
  两个前端宿主                 共享客户端库（antidebug_client 逻辑）
┌─────────────────┐   ┌─────────────────┐
│ x64dbg 插件      │   │ antictl 命令行   │   ← 只认客户端 API
│ dp64 / dp32     │   │ load/hide/smoke │
└───────┬─────────┘   └───────┬─────────┘
        └──────────┬──────────┘
                   ▼
     ┌──────────────────────────────┐
     │ DriverControlChannel         │ ★C 可换：控制传输（现在是 IOCTL）
     ├──────────────────────────────┤
     │ DriverLoader（加载编排）      │
     │  ├ IPrimitiveProvider        │ ★B 可换：内核原语来源
     │  │   └ IntelNalProvider      │    （iqvw64e.sys，vtdbg 同款）
     │  ├ IImageMapper              │    重定位 + 修导入 + 拷贝
     │  │   └ KdmapperMapper        │
     │  └ IEntryInvoker             │    在内核里调用驱动入口
     │      └ NtAddAtomInvoker      │
     └──────────────────────────────┘
════ IOCTL（\\.\AntidbgCtrl，协议见 docs/03）═════════════════════════
内核态 ────────────────────────────────────────────────────────────────
┌ antidebug.sys（手动映射，无 CRT/异常/RTTI）──────────────────────────┐
│ 控制层   设备对象 + IOCTL 分发 + 密钥校验        ← 唯一能改"策略"的层 │
│ 框架层   技术注册表（一张声明式表）+ 策略（PID 名单 + 技术开关位）    │
│ 技术层   10 个处理函数（vmp 移植）：纯业务，不知道自己被谁钩进来      │
│ 引擎层   IHookEngine ← InfinityHookProEngine    ← ★A 可换：hook 机制 │
│ 平台层   模块枚举/特征码扫描/KVAS 影子解析/HDE64 反汇编 ← 吸收系统差异 │
│ 启动层   CustomDriverEntry + 启动邮箱握手（vtdbg 式）                │
└─────────────────────────────────────────────────────────────────────┘
```

**依赖规则**：只准上层调用下层；跨层只能走图里画出的接口。
两个硬性规定：

1. 技术层**不许** include 引擎层的头文件——处理函数只依赖"原函数指针 + 策略查询"。
2. `antidebug/include/adbg_abi.h` 是用户态与内核态之间**唯一**的共享头文件，
   两边谁都不许私下再定义协议结构。

## 3. 三个"可换"接口（抗变化的关键）

### ★A. IHookEngine —— hook 机制可换

```cpp
// driver/src/engine/hook_engine.hpp
// 回调签名：与 InfinityHook 引擎原生回调一致（参数直接透传）
using SyscallHookCallback = void(__fastcall*)(unsigned long index,
                                              void** system_call_function);
// system_call_function 指向"本次系统调用要跳去的内核函数地址"，
// 回调里把它改写成处理函数地址即完成一次拦截。
//
// 说明：内核镜像无 CRT（docs/02 §6），接口用"函数指针结构体"而不是
// C++ 虚类——效果等价于 vtable，但不需要构造函数，全局可零初始化。
struct AD_HOOK_ENGINE {
    NTSTATUS (*Start)(const SyscallHookCallback callback);
    NTSTATUS (*Stop)();                 // 必须完整还原所有改过的指针
    BOOL      (*IsRunning)();
};
extern const AD_HOOK_ENGINE InfinityHookProEngine;  // 默认实现
// 换引擎 = 写一个新的 const AD_HOOK_ENGINE，一行换挂
```

默认实现 `InfinityHookProEngine`（移植自参考克隆）：改 CKCL 日志会话的取时钟指针，
在每次系统调用时收到回调。**为什么留接口**：这套手法依赖 ETW 内部结构，微软一个
更新就可能失效；真到那天，换一个实现（比如 instrumentation callback 方案），
上面四层一行不改。

### ★B. IPrimitiveProvider —— 内核原语来源可换

```cpp
// loader/src/spi/primitive_provider.hpp
class IPrimitiveProvider {
public:
    virtual NTSTATUS Load() = 0;    // 把"漏洞驱动"装起来
    virtual NTSTATUS Unload() = 0;  // 卸掉并清理痕迹
    virtual bool ReadKernel(uint64_t kva, void* buffer, size_t size) = 0;
    virtual bool WriteKernel(uint64_t kva, const void* buffer, size_t size) = 0;
    virtual uint64_t AllocPool(size_t size) = 0;
    virtual bool FreePool(uint64_t kva) = 0;
    virtual uint64_t CallKernel(uint64_t func, uint64_t a1, uint64_t a2,
                                uint64_t* out) = 0;  // 在内核里执行指定函数
};
```

默认实现 `IntelNalProvider`：包装移植来的 kdmapper 代码（iqvw64e.sys）。
**为什么留接口**：iqvw64e 在微软"易受攻击驱动阻止列表"里，环境一旦开启阻止就会
加载失败；届时新增一个 `CedriverProvider`（CE 的 dbk64.sys，docs/09）即可，
映射器、客户端、插件全部不用动。

配套两个小接口（由同一份 kdmapper 移植代码实现）：

- `IImageMapper`：把驱动 PE 镜像重定位、修导入、拷进内核内存；
- `IEntryInvoker`：通过劫持 `nt!NtAddAtom`（把一条 `mov rax,目标; jmp rax` 写进
  该函数开头，调用后还原）在内核里执行驱动的入口函数。

### ★C. 技术注册表 —— 技术集可扩展

```cpp
// driver/src/framework/technique.hpp —— 一张声明式表，框架按表驱动
struct TechniqueDescriptor {
    const char* nt_name;    // 要拦的内核函数名，如 "NtQueryInformationProcess"
    uint32_t    tech_mask;  // 这一项服务哪些技术位（见 docs/03 的 11 个技术位）
    void*       handler;    // 处理函数（与 nt_name 的原型一致）
    uint32_t    flags;      // 预留
};
extern const TechniqueDescriptor kTechniques[];
extern const ULONG kTechniqueCount;
```

**对比 vmp 的做法**（if/else 连锁 + 5 处修改）：加一项新技术 = 加一个表项 + 一个
处理函数文件，派发、策略、UI 清单全部自动跟上（UI 的技术列表就是通过 IOCTL
从这张表动态查出来的，前端不写死）。

## 4. 三条运行路径

### 4.1 加载（用户点"启动"或 `antictl load`）

```
1. IntelNalProvider.Load()
     把内嵌的 iqvw64e.sys 字节写到 %TEMP%\随机名 → 建注册表服务项 →
     NtLoadDriver → 打开 \\.\Nal → 自检（读 ntoskrnl 头）→ 清加载痕迹
2. 分配一块内核内存做"启动邮箱"，填好尺寸/版本/魔数/期望 ABI 版本，
   再生成 16 字节随机"会话密钥"放进邮箱，回读校验
3. KdmapperMapper：分配内核内存，把 antidebug.sys 重定位、修导入后拷进去
4. NtAddAtomInvoker 调 CustomDriverEntry(邮箱指针, 魔数)
     入口只做校验（几十微秒就返回）：校验邮箱 → 清零全局区 →
     起一个系统线程去干慢活 → 回写"线程已创建"
5. 加载器每 50ms 读一次邮箱，最多等 10 秒：
     成功 → 取出会话密钥，释放邮箱，卸载 iqvw64e（覆写临时文件后删除）
     失败 → 读出失败步骤码报告给用户；内核里的内存不回收（宁可泄露到重启，
            也不能释放可能还在跑的代码——vtdbg 验证过的教训）
6. 打开 \\.\AntidbgCtrl，用密钥做 SESSION_INIT，进入可用状态
```

### 4.2 热路径（每次系统调用，性能敏感）

```
引擎回调(系统调用号, &目标函数地址)
  → 策略快查①：当前进程在隐藏名单里吗？不在 → 直接返回（零成本放行）
  → 注册表查表：目标地址 == 某个已解析的原函数地址？
      命中 → 把目标地址换成处理函数
  → 系统调用落进处理函数
  → 处理函数：内核模式来的调用直接放行；用户模式来的
      → 策略快查②：这项技术的开关位是开着的吗？关着 → 只调原函数
      → 开着 → 调原函数，把结果改写成"未被调试"，返回
```

热路径上的铁律：**不加锁、不分配内存、不打日志**（调试日志用编译开关控制，
默认关闭）。策略数据用 `volatile` 读 + `Interlocked` 写，理由：这些数据
极少变化（人手点 UI 才变），读写最多差一拍（比如刚关掉的开关多拦了一次），
无害；换来的是热路径零等待。

### 4.3 停止（用户点"停止"或 `antictl unload`）

```
DRIVER_STOP(IOCTL)
  → 引擎 Stop()：还原所有改过的指针（GetCpuClock / HvlGetQpcBias /
      物理机性能计数器那一组，见 docs/04），停 CKCL 会话，收检测线程
  → 删设备对象和符号链接
  → 状态标记为"已停止"
```

**必须知道的限制**：手动映射进内核的镜像**卸不掉**，停止只是让它不再拦截。
彻底清除要重启电脑。这是这条技术路线的固有代价（vtdbg 同样如此）。

## 5. 模块与目录对照

| 层 | 代码位置 | 内容 |
|---|---|---|
| 协议 | `include/adbg_abi.h` | IOCTL 码、全部收发结构、技术位定义、启动邮箱、魔数 |
| 启动层 | `driver/src/entry.cpp` | CustomDriverEntry + 启动线程 |
| 平台层 | `driver/src/platform/` | utils（模块枚举/特征码/KVAS/HDE64 调用）、hde/ |
| 引擎层 | `driver/src/engine/` | hook_engine.hpp（接口）+ infinity_hook_pro（实现） |
| 框架层 | `driver/src/framework/` | technique（注册表）+ policy（策略） |
| 技术层 | `driver/src/techniques/` | 按类拆分的 10 个处理函数 |
| 控制层 | `driver/src/control/` | 设备 + IOCTL 分发 + 密钥校验 |
| 加载 SPI | `loader/src/spi/` | 三个接口头文件 |
| kdm 移植 | `loader/src/kdm/` | intel_driver / service / kdmapper / utils 移植 |
| 控制客户端 | `client/` | antidebug_client 库（双架构）：IOCTL 封装 + 密钥注册表交接 |
| 加载编排 | `loader/src/driver_loader.*` | 高层 Load/Unload/IsLoaded + 密钥交接 |
| 命令行 | `app/antictl/` | load/unload/status/hide/unhide/tech/smoke |
| 插件 | `plugin/src/` | pluginmain（导出）+ plugin（菜单/命令/事件）+ driver_client + ui/ |

## 6. 关键决策记录（为什么这么定）

| 决策 | 备选方案 | 不选备选的原因 |
|---|---|---|
| 派发按"函数地址"匹配 | 按系统调用号（SSDT 索引）匹配 | 调用号每个 Windows 版本都可能变；地址用 `MmGetSystemRoutineAddress` 拿，天然跟随当前系统。vmp 已验证 |
| 控制面用命名设备 + IOCTL | 共享内存轮询；像 vtdbg 那样借映射原语通信 | 插件在驱动加载后仍要与驱动长期对话；IOCTL 简单、双进程可用（dp32/dp64/antictl）；WOW64 下 struct 固定宽度即可安全 |
| 驱动入口用 (邮箱, 魔数) 参数 | 伪造 DRIVER_OBJECT | 邮箱协议让"加载窗口"极短且可确认成败（vtdbg 验证）；伪造对象反而要猜内核结构 |
| dp32 不带加载器 | 32 位进程也做 kdmapper | iqvw64e 的 IOCTL 结构按 64 位指针设计，WOW64 下直接用会踩坑；x32dbg 场景由 dp64 或 antictl 先装驱动即可 |
| 会话密钥放注册表交接 | 只放在加载进程内存里 | dp32 插件/新开的 x64dbg 实例拿不到进程内存里的密钥；注册表项（HKLM\SOFTWARE\Antidbg）只允许管理员读写，被调试进程（通常非管理员）读不到。够用，docs/06 有风险说明 |
| 镜像失败时宁可泄露不回收 | 失败时 FreePool | 无法判断入口是否已经跑起来；释放可能还在执行的代码 = 蓝屏。vtdbg 里 review 过的结论，直接继承 |

## 7. 里程碑拆解

| 里程碑 | 范围 | 完成标志 |
|---|---|---|
| M0a | 文档全套 | 本套文档 |
| M0b | 构建骨架 | 两个 preset 能配置出空目标 |
| M1 | 驱动 | antidebug.sys 编译通过，五层文件齐 |
| M2 | 加载器 + antictl | x64 链路编译通过，`antictl status` 可跑（不加载） |
| M3 | 插件 | dp64/dp32 编译通过，菜单/命令/对话框齐 |
| M4 | 验证与回填 | 全量零告警（目标），文档与实际一致 |
