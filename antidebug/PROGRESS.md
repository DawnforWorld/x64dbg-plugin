# PROGRESS.md —— antidebug 进度看板

> 维护规则：每完成一个里程碑或修复一个重要问题就更新这里。
> 最近更新：2026-09-20

## 当前状态一览

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0a | 文档全套（docs 00-09 + 根 README/AGENTS/CONTRIBUTING + 本看板） | ✅ 完成 |
| M0b | 构建骨架（根/插件 CMake + presets + FindWDK + 字节转头文件脚本） | ⬜ 未开始 |
| M1 | 内核驱动（入口/引擎/技术注册表/10 个钩子/控制设备） | ⬜ 未开始 |
| M2 | 加载器（kdmapper 移植 + 三个 SPI + driver_loader）与 antictl | ⬜ 未开始 |
| M3 | x64dbg 插件（菜单/命令/事件/控制面板 UI） | ⬜ 未开始 |
| M4 | 全量编译验证（x64 全量 + x86 插件）与文档回填 | ⬜ 未开始 |

里程碑细节见 `docs/02-architecture.md` 第 8 节的拆解。

## 待办（按优先级）

- [ ] M0b~M4：按下表推进
- [ ] 用户在管理员终端实机执行 `antictl smoke`（完整加载→查询→停止循环），见 docs/07
- [ ] 用 `D:\project\vmp\example-vmp\v1-反调试\example.vmp.exe` 做逐项技术验证（docs/07 表格）
- [ ] 虚拟机里验证 Win11 路径（引擎的 HvlGetQpcBias 分支）
- [ ] 未来：CE dbk64 原语提供者实现（docs/09，仅当 iqvw64e 被全面封禁时再做）

## 环境验证记录

（暂无——编译验证在 M4 记录）

## 变更日志

- 2026-09-20 M0a：设计定稿；文档全套落地（docs 00-09 + 根三件套 + 本看板）。
