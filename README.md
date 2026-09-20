# x64dbg-plugin 插件合集

这是一个用 CMake 统一管理的 x64dbg 插件合集工程。每个插件放在自己的目录里，互不干扰；
想加第二个插件时，只需要建一个新目录，然后在根 CMakeLists.txt 里加一行。

> x64dbg 是 Windows 上常用的开源调试器（逆向分析工具）。给它写插件，就是编译出一个
> 特殊的 DLL（.dp64 / .dp32），放进 x64dbg 的 plugins 目录，x64dbg 启动时会自动加载。

## 目录结构

```
x64dbg-plugin/
├── CMakeLists.txt          # 根构建脚本：只负责把各插件目录加进来
├── CMakePresets.json       # 一键配置的"预设"（debug / release / x86 / x64）
├── AGENTS.md               # 给 AI 编程助手（和新人）看的工作规范
├── CONTRIBUTING.md         # 代码风格与提交规范
├── cmake/                  # 共享的构建辅助脚本（找 WDK、生成驱动字节头文件）
└── antidebug/              # 第一个插件：反反调试（详见其目录内 README）
```

## 目前包含的插件

| 目录 | 名称 | 功能一句话 | 状态 |
|---|---|---|---|
| antidebug/ | AntiDebug | 内核驱动 + 加载器 + 插件三件套，调试带保护的目标程序时把调试器"藏起来" | 开发中 |

## 怎么构建

前置条件（详见 `antidebug/docs/05-build-guide.md`）：

1. Visual Studio 2019 或 2022（含 C++ 桌面开发组件）
2. Windows Driver Kit（WDK）10，只需要装好，不需要配环境变量
3. CMake 3.24 以上

```bash
# 生成 x64 全量构建（驱动 + 加载器 + 命令行工具 + 插件）
cmake --preset win-x64-release
cmake --build build/win-x64-release --config Release

# 只构建 32 位插件（给 x32dbg 用；驱动本身只有 64 位）
cmake --preset win-x86-release
cmake --build build/win-x86-release --config Release
```

构建产物位置见 `antidebug/docs/05-build-guide.md`。

## 怎么新增第二个插件

1. 新建目录，比如 `plugins2/`，里面放一个 `CMakeLists.txt` 和源码；
2. 在根 `CMakeLists.txt` 末尾加一行 `add_subdirectory(plugins2)`；
3. 插件目录里照抄 `antidebug/plugin/CMakeLists.txt` 的写法（输出 .dp64/.dp32、
   链接 pluginsdk），把 `antidebug/plugin/third_party/pluginsdk` 复制或引用过去。

## 参考项目（本地克隆，不入本仓库）

`antidebug/` 目录下有两个参考用的第三方克隆，它们各自是独立 git 仓库，
本仓库通过 .gitignore 排除它们：

| 目录 | 是什么 | 我们拿来做什么 |
|---|---|---|
| `antidebug/InfinityHookPro` | ETW 时钟钩子引擎（Win7~Win11 24H2），MIT | 驱动的 hook 引擎移植来源 |
| `antidebug/TitanHide` | 经典的隐藏调试器驱动 + x64dbg 插件，MIT | 插件 SDK（pluginsdk）来源、插件骨架参考 |

另外两个不在本仓库里的参考工程（位于本机其他路径，文档中会引用路径）：

- `D:\project\vtdbg`：驱动加载手法的来源（kdmapper 手动映射 + iqvw64e）
- `D:\project\vmp\vmp`：反反调试业务逻辑的来源（10 个 syscall 钩子的处理函数）

## 许可

本工程采用 MIT 许可。移植的第三方代码保留原许可声明，见
`antidebug/docs/06-security-and-ops.md` 的「代码来源与许可」一节。
