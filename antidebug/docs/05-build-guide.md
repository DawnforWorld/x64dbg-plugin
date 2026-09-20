# 05 —— 构建指南

| | |
|---|---|
| 状态 | 定稿 |
| 日期 | 2026-09-20 |

## 1. 环境要求

| 工具 | 版本 | 说明 |
|---|---|---|
| Visual Studio | 2019（v142）或 2022（v143） | 勾选"使用 C++ 的桌面开发" |
| Windows Driver Kit (WDK) | 10（推荐 10.0.19041.0） | 只为拿内核头文件和 ntoskrnl.lib/hal.lib；FindWDK 会自动找，装了即可 |
| CMake | ≥ 3.24 | 自带 cmake-gui/CLI 均可 |
| Windows | x64 的 Win10/Win11 | 构建机即目标机也可 |

不需要：Python、NASM、WDK 的 VS 插件集成（我们用纯 CMake + 导入库编驱动，
这条路 `D:\project\vtdbg` 已经验证过）。

## 2. 一键构建

```bash
# x64 全量：驱动 + 加载器 + antictl + antidebug.dp64
cmake --preset win-x64-release
cmake --build build/win-x64-release --config Release

# 32 位插件：antidebug.dp32（给 x32dbg；不带驱动和加载器）
cmake --preset win-x86-release
cmake --build build/win-x86-release --config Release
```

调试版把 `release` 换成 `debug`。四个预设都定义在根目录 `CMakePresets.json`，
用 Visual Studio 生成器（和 vtdbg 一致，驱动链接选项在 VS 工具链下最稳）。

## 3. iqvw64e.sys 字节的提供方式（重要）

加载器需要 Intel 网卡驱动 iqvw64e.sys 的字节（手动映射的"梯子"）。
它的**真实字节永远不进 git**（.gitignore 已挡），两种提供方式：

```bash
# 方式 A（推荐）：直接复用 vtdbg 已生成的头文件
cmake --preset win-x64-release --fresh \
  -DADBG_IQVW64E_HPP=D:/project/vtdbg/build/gen/intel_driver_resource.hpp

# 方式 B：给原始 .sys，构建时自动转成头文件
cmake --preset win-x64-release --fresh \
  -DADBG_IQVW64E_PATH=D:/drivers/iqvw64e.sys
```

两种方式产物都进 `build/*/gen/`（不进源码树）。都不提供时用仓库里的**空桩**
（字节数为 0），加载器会在第一步就报"资源未生成"并退出——这是故意的
防呆，不是 bug。

`iqvw64e.sys` 原始文件从哪来：它随 kdmapper 项目流通（如 TheCruZ/kdmapper
仓库），Intel 官方网卡驱动包里也有。自行确认你有权下载和使用。

### 3.1 杀毒软件会删你的产物（重要）

给构建提供真实 iqvw64e 字节后，**内嵌这些字节的 EXE（antictl.exe）会被
Windows 安全防护在生成后几秒内自动删除**（实测 2026-09-20：同内容改名
的副本也一起被删，是按内容识别；Defender 操作日志里没有对应检测记录）。
`.dp64` 插件是 DLL、同样内嵌字节，实测未被删；被定点清理的只有 EXE。

处理方式（改系统安全策略，按 AGENTS.md 规则由你本人手动做）：
管理员 PowerShell 给构建目录加排除项后重编 antictl：

```powershell
Add-MpPreference -ExclusionPath "D:\project\x64dbg-plugin\build"
```

或者不用 antictl，改用 dp64 插件菜单里的"加载并启动"（插件内嵌了完整
加载链，能自己装驱动，见 docs/07）。

另一个相关的坑（已修复，2026-09-20）：`loader/CMakeLists.txt` 的 include
目录顺序曾经把生成头目录排在最后，导致 `kdm/include` 里的空桩先被命中、
真实字节永远编不进去（症状：配置了 -D 参数、日志也显示复制了资源头，
但二进制大小不变、行为仍是"资源未生成"）。现在生成头目录固定排第一，
顺序别改回去。

## 4. 产物清单

| 产物 | 路径（Release 配置） | 用途 |
|---|---|---|
| antidebug.sys | `build/win-x64-release/antidebug/driver/Release/` | 驱动本体（不直接部署；字节已内嵌进插件/antictl） |
| antictl.exe | `build/win-x64-release/antidebug/app/antictl/Release/` | 命令行工具 |
| antidebug.dp64 | `build/win-x64-release/antidebug/plugin/Release/` | x64dbg 插件 |
| antidebug.dp32 | `build/win-x86-release/antidebug/plugin/Release/` | x32dbg 插件 |
| antidebug_loader.lib | `build/win-x64-release/antidebug/loader/Release/` | 加载器静态库（内部产物） |
| antidebug_client.lib | `build/win-*/antidebug/client/Release/` | 控制面客户端库（内部产物，双架构） |
| 生成的头文件 | `build/*/gen/` | 驱动字节头、iqvw64e 字节头（不入库） |

## 5. 部署

1. 把 `antidebug.dp64` 拷到 x64dbg 的 `x64\plugins\`，`antidebug.dp32` 拷到
   `x32\plugins\`（32 位插件只做控制，需要先用 x64dbg 或 antictl 装好驱动，
   见 docs/02 §6 的决策说明）。
2. `antictl.exe` 放哪都行（自包含）。
3. 重启 x64dbg，"插件"菜单出现 AntiDebug 即成功。

## 6. 构建系统怎么组织的

```
根 CMakeLists.txt            只做一件事：add_subdirectory(antidebug)
CMakePresets.json            4 个预设（x64/x86 × debug/release）
cmake/FindWDK.cmake          找 Windows Kits（优先 10.0.19041.0）——自 vtdbg 移植
cmake/BinToHeader.cmake      脚本模式：二进制 → C++ 字节数组头文件
third_party/x64dbg-pluginsdk/ 共享插件 SDK（所有插件引用这一份）
antidebug/CMakeLists.txt     插件级编排 + 选项（ADBG_BUILD_DRIVER 等）
antidebug/driver/            antidebug.sys（vtdbg 同款链接配方，见下）
antidebug/loader/            静态库（x64 only）
antidebug/app/antictl/       CLI
antidebug/plugin/            dp32/dp64（两个预设各编一次）
```

**驱动链接配方**（为什么这么怪，见 CONTRIBUTING.md「内核驱动的特殊约束」）：
`/kernel /GS- /GR- /EHs-c-` + `/SUBSYSTEM:NATIVE /ENTRY:CustomDriverEntry
/NODEFAULTLIB /DRIVER`，链接 `ntoskrnl.lib` + `hal.lib` + `wdmsec.lib`，
必须保留 `.reloc`（映射器要按随机基址重定位）。

**字节内嵌链**：先编出 `antidebug.sys` → CMake 自定义命令把它转成
`antidebug_driver_resource.hpp`（`antidebug_driver_resource` 命名空间）→
loader 和 antictl 编译时 include。所以"构建 x64 全量"会自动完成内嵌，
不需要手动拷贝驱动文件。

## 7. 常见问题

| 现象 | 原因与处理 |
|---|---|
| 找不到 WDK | FindWDK 只认 `C:\Program Files (x86)\Windows Kits\10` 等固定位置；确认装的是 WDK10 且有 `Lib/<版本>/km/x64` |
| 链接报 `unresolved external __security_cookie` 之类 | 有人给驱动目标开了 /GS 或加了 CRT 依赖，回看配方 |
| antictl 报"iqvw64e resource not generated" | 正常防呆：按 §3 提供 -D 参数重新配置 |
| x86 预试编了驱动 | 不会——`ADBG_BUILD_DRIVER` 在 x86 预设里是 OFF，只编插件 |
| 想清掉重来 | 删 `build/` 目录，或加 `--fresh` |
