# PROGRESS.md —— antidebug 进度看板

> 维护规则：每完成一个里程碑或修复一个重要问题就更新这里。
> 最近更新：2026-09-20

## 当前状态一览

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0a | 文档全套（docs 00-09 + 根 README/AGENTS/CONTRIBUTING + 本看板） | ✅ 完成 |
| M0b | 构建骨架（根/插件 CMake + presets + FindWDK + BinToHeader） | ✅ 完成 |
| M1 | 内核驱动（入口/引擎/技术注册表/10 个钩子/控制设备） | ✅ 完成，编译通过 |
| M2 | 加载器（kdmapper 移植 + SPI + driver_loader + antictl） | ✅ 完成，编译通过 |
| M3 | x64dbg 插件（菜单/命令/事件/控制面板 UI） | ✅ 完成，编译通过 |
| M4 | 全量编译验证（x64 全量 + x86 插件）与文档回填 | ✅ 完成 |

里程碑细节见 `docs/02-architecture.md` 第 8 节的拆解。

## 环境验证记录

| 日期 | 环境 | 结果 |
|---|---|---|
| 2026-09-20 | VS2022(v143) + WDK 10.0.19041.0 + CMake 4.4，Win10 19045 | 见下方「构建验证记录」 |

## 待办（按优先级）

- [ ] **用户实机验证**：管理员终端跑 `antictl smoke`（完整加载→查询→停止循环，docs/07 T3）
- [ ] 用 `D:\project\vmp\example-vmp\v1-反调试\example.vmp.exe` 做逐项技术验证（docs/07 T4 表格）
- [ ] 虚拟机里验证 Win11 路径（引擎的 HvlGetQpcBias 分支 + 虚拟机形态）
- [ ] 未来：CE dbk64 原语提供者实现（docs/09，仅当 iqvw64e 被全面封禁时再做）
- [ ] 未来：会话密钥用 DPAPI 加密存注册表（docs/06 §4 的残余风险）

## 构建验证记录

### 2026-09-20（M4，Release）

- `cmake --preset win-x64-release` + `--build --config Release`：**通过，零错误零告警**。
  产物：`antidebug.sys`（42KB）、`antictl.exe`、`antidebug.dp64`（2.7MB 静态 CRT）、
  `antidebug_loader.lib`、`antidebug_client.lib`、`gen/antidebug_driver_resource.hpp`
  （驱动字节内嵌头，176KB 源码形式）。
- `cmake --preset win-x86-release` + `--build --config Release`：**通过**。
  产物：`antidebug.dp32`（156KB，仅控制面，无加载器）。
- `dumpbin /exports` 验证 dp64/dp32 均导出 `pluginit/plugstop/plugsetup`（SDK v1 要求）。
- 离线自测（T2）：`antictl version` → 打印协议版本正常；
  `antictl status` → 正确报告"驱动未运行"（退出码 2）。
- 构建未提供 iqvw64e 字节（空桩）：`antictl load` 会在第一步给出明确提示——
  这是设计内的防呆，不是缺陷。

### 2026-09-20（补：真实 iqvw64e 字节链路打通）

- 用 `-DADBG_IQVW64E_HPP=D:/project/vtdbg/build/gen/intel_driver_resource.hpp`
  重新配置并干净重编 x64：dp64（457KB）与 antictl.exe（437KB）均**确认内嵌
  真实 iqvw64e 字节**（脚本比对字节序列命中），dumpbin 复核导出完好。
- **修复一个隐藏 bug**：`loader/CMakeLists.txt` 的 include 顺序曾把生成头目录
  排在最后，空桩永远先被命中——配置了真实字节也编不进去（症状：二进制大小
  不变）。已把 `ADBG_GEN_DIR` 固定排第一，勿回退。
- **发现环境问题（未解决，待用户操作）**：内嵌 iqvw64e 字节的 **EXE 会被
  Windows 安全防护在生成后数秒内删除**（antictl.exe 反复消失，改名副本同删；
  Defender 操作日志无检测记录）。dp32/dp64/sys 未被删。处理见 docs/05 §3.1
  （给 build 目录加杀软排除项，需用户管理员手动执行），或改用插件菜单
  "加载并启动"。

## 变更日志

- 2026-09-20 M0a：设计定稿，文档全套落地。
- 2026-09-20 M0b：CMake 骨架 + 4 预设 + FindWDK + BinToHeader。
- 2026-09-20 M1：驱动六层落地（入口/平台/引擎/框架/技术/控制）；
  InfinityHookPro 引擎移植并保留完整还原路径；vmp 10 个钩子语义原样移植
  + 按进程总闸门 + 11 项技术开关；wdmsec 无 CRT 兼容层；
  evntrace 内核态缺结构改为自定义；Debug 配置去除 /RTC1。
- 2026-09-20 M2：client 双架构控制面库（IOCTL + 密钥注册表交接）；
  kdmapper 移植（池标签 'Adbg'，MIT 许可随附）；driver_loader 加载窗口
  （邮箱协议 + "宁可泄露不回收"失败语义）；antictl 八个子命令。
- 2026-09-20 M3：插件骨架（SDK v1，bridgemain 先行的包含顺序）；
  菜单/命令/事件显式注册（CB_MENUENTRY 与枚举撞名，改 _plugin_registercallback）；
  控制面板 UI（运行时创建控件，技术清单动态枚举，慢操作走 worker 线程）；
  ini 持久化（自动隐藏 + 技术开关记忆）。
- 2026-09-20 M4：全量 Release 编译验证通过；dumpbin 导出核验；文档回填
  （03 DriverBase 说明、02/05 补 client 库）。
- 2026-09-20 修复：loader include 顺序 bug（生成头被空桩遮蔽，真实
  iqvw64e 字节编不进去）+ 记录杀软删 EXE 产物的环境问题（docs/05 §3.1）。
- 2026-09-20 补：实机首测报 0xC0000061（SeDebugPrivilege 取不到，vtdbg
  手法在此硬失败）。按用户要求保持与 vtdbg 逐字一致、不改判断逻辑；
  在 antidebug 自己的 driver_loader 层给该状态码和注册表写失败补了
  可操作的中文提示（docs/05 §7）。
