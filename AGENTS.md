# AGENTS.md —— 本仓库的工作规范

给 AI 编程助手和任何接手的人看的规则。做任何事之前先读完这一页。

## 一句话背景

本仓库是 x64dbg 插件合集。第一个插件 antidebug = 内核驱动（把调试器藏起来，反反调试）
+ 用户态加载器（kdmapper 手动映射）+ x64dbg 插件（带控制界面）。
核心设计文档在 `antidebug/docs/`，进度看板在 `antidebug/PROGRESS.md`。

## 必读文件（按顺序）

1. `README.md`（根）—— 工程定位
2. `antidebug/docs/00-overview.md` —— antidebug 总览
3. `antidebug/docs/02-architecture.md` —— 分层与接口，改代码前必读
4. `antidebug/docs/03-api-spec.md` —— 用户态与内核态之间的协议，改协议必须先改这份文档
5. `antidebug/PROGRESS.md` —— 当前做到哪了

## 硬性规则

1. **文档先行**：改接口/改行为，先改 `antidebug/docs/` 里对应的文档，再改代码。
2. **进度看板必须更新**：每完成一个里程碑或修复一个重要问题，更新 `antidebug/PROGRESS.md`。
3. **文档用平实的语言**：不用生僻词、不堆术语。必须用的英文术语（IOCTL、syscall、ETW 等）
   第一次出现时用一句话解释。写给"半年后忘了细节的自己"看。
4. **提交习惯**：一个提交做一件事；提交说明第一行用中文短句概括（如"M1: 移植 vmp 的
   10 个钩子处理函数"）。不要把文档和大量代码混在一个提交里。
5. **不入库的东西**：构建产物（build/）、参考克隆（antidebug/InfinityHookPro、
   antidebug/TitanHide）、iqvw64e.sys 的真实字节（任何情况都不提交，只提交空桩）。
6. **不在自动化流程里加载驱动**：驱动加载需要管理员权限且影响整机内核状态，
   只允许用户本人手动执行。构建、编译、`antictl status` 这类只读操作可以做。
7. **参考克隆是只读的**：不要修改 antidebug/InfinityHookPro 和 antidebug/TitanHide
   里的任何文件。要改的东西，移植进我们自己的源码树再改。

## 构建与验证命令

```bash
# x64 全量（驱动 + 加载器 + antictl + dp64 插件）
cmake --preset win-x64-release
cmake --build build/win-x64-release --config Release

# 32 位插件（仅插件，dp32）
cmake --preset win-x86-release
cmake --build build/win-x86-release --config Release

# 只读自测（不加载驱动，普通权限可跑）
build/win-x64-release/app/antictl/Release/antictl.exe status
```

驱动加载/卸载（`antictl load|unload|smoke`）**必须由用户在管理员终端手动执行**，
自动化脚本和 AI 助手一律不执行。

## 代码风格速记（详见 CONTRIBUTING.md）

- 用户态 C++：Google C++ Style Guide 的命名习惯（文件小写下划线、类型大驼峰、
  常量 k 前缀），缩进 4 空格。
- 内核驱动：同样命名习惯，但受"手动映射"约束——禁用 CRT、异常、RTTI、栈保护；
  全局变量必须零初始化；不用需要 CRT 初始化的对象。
- 注释写"为什么"，不写"这行在干什么"。中文注释为主。

## 目录速查

| 路径 | 内容 |
|---|---|
| `antidebug/include/adbg_abi.h` | 用户态/内核态共享的协议头（IOCTL 码、结构体） |
| `antidebug/driver/` | antidebug.sys 内核驱动源码 |
| `antidebug/loader/` | 用户态加载器（kdmapper 移植 + 高层封装） |
| `antidebug/app/antictl/` | 命令行工具（load/unload/status/hide/smoke） |
| `antidebug/plugin/` | x64dbg 插件（菜单、命令、设置对话框） |
| `antidebug/docs/` | 全部设计文档（00~09） |
| `antidebug/PROGRESS.md` | 进度看板 |
