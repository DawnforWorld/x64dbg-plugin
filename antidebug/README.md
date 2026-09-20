# AntiDebug —— x64dbg 反反调试插件

**它解决什么问题**：给软件加壳的公司（比如 VMProtect）会在程序里做各种"反调试"检查，
一旦发现被调试器附加就退出或乱跳。用 x64dbg 调试这类程序时，需要一个工具把这些
检查全部"骗过去"。这个插件就是干这个的，业内叫"反反调试"（anti-anti-debug）。

**它由三部分组成**：

1. **antidebug.sys（内核驱动）**——真正干活的。它在内核里拦截 10 个跟调试相关的
   系统调用，把结果改写成"没有调试器"的样子，让目标程序的检查落空。
2. **加载器（用户态库）**——驱动没有数字签名，不能正常加载。加载器借用一个有签名的
   Intel 网卡驱动（iqvw64e.sys）的漏洞，把我们的驱动"手动映射"进内核运行。
   这套手法来自 `D:\project\vtdbg` 工程。
3. **x64dbg 插件（.dp64/.dp32）**——你在调试器里看到的界面：菜单、命令、
   设置对话框，负责加载/停止驱动、隐藏当前被调试的进程、开关各项技术。

**业务逻辑来源**：`D:\project\vmp\vmp` 工程（10 个系统调用钩子的处理函数，语义原样保留）。
**hook 引擎来源**：本目录下的 `InfinityHookPro/` 参考克隆（Win7~Win11 24H2 全兼容）。

## 快速上手

详细的安装和使用步骤看 `docs/05-build-guide.md`（构建）和 `docs/07-test-plan.md`（验证）。
最简流程：

```bash
# 1. 构建（x64 全量）
cmake --preset win-x64-release
cmake --build build/win-x64-release --config Release

# 2. 把插件 DLL 拷进 x64dbg 的 plugins 目录，重启 x64dbg

# 3. 管理员终端里先用命令行工具冒烟测试驱动
antictl.exe smoke

# 4. 之后在 x64dbg 菜单里操作：AntiDebug -> 控制面板
```

## 文档目录

| 文档 | 内容 |
|---|---|
| `docs/00-overview.md` | 总览：做什么、不做什么、怎么分工 |
| `docs/01-requirements.md` | 需求清单与使用场景 |
| `docs/02-architecture.md` | 架构：分层、接口、三条运行路径 |
| `docs/03-api-spec.md` | 接口规范：IOCTL 协议、技术清单、插件命令 |
| `docs/04-os-compatibility.md` | Windows 版本兼容矩阵（Win7~Win11 24H2） |
| `docs/05-build-guide.md` | 构建指南：环境、命令、产物 |
| `docs/06-security-and-ops.md` | 运行条件、风险与限制、代码来源与许可 |
| `docs/07-test-plan.md` | 测试计划（含 VMProtect 样本验证步骤） |
| `docs/08-porting-vmp.md` | 从 vmp 移植的对照表 |
| `docs/09-ce-driver-alternative.md` | 备选方案：CE 驱动（dbk64.sys） |
| `PROGRESS.md` | 进度看板 |

## 一句话原理（想深入了解再看 docs/02）

Windows 的系统调用要经过一张内核函数表（SSDT）。我们的驱动不改这张表（改了会触发
内核的自我保护机制 PatchGuard 导致蓝屏），而是利用系统自带的 ETW 事件机制
（Event Tracing for Windows，Windows 的内核事件记录系统）里的一个时钟函数指针，
在每次系统调用发生时拿到回调机会，把"即将跳转到的内核函数地址"临时换成我们的
处理函数——处理完再把结果改写成"未被调试"。这套手法叫 InfinityHook，
对 Win7 到 Win11 24H2 都有效。

## 用途限定

本插件仅用于**授权的**软件逆向分析、安全研究和教学。请遵守当地法律，
不要用于侵害他人软件权益的用途。
