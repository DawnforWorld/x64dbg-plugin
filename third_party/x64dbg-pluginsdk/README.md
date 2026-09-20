# third_party/x64dbg-pluginsdk —— x64dbg 插件 SDK 共享副本

## 这是什么

x64dbg 官方插件 SDK（头文件 + 导入库），本合集的**所有插件共同引用这一份**，
任何插件不要再往自己目录里拷贝。

## 来源与取得方式

- 来源：x64dbg 官方仓库的 `pluginsdk/` 目录（插件 SDK，随 x64dbg 主程序版本走）
- 本副本经 `antidebug/TitanHide/TitanHide_x64dbg/pluginsdk`（TitanHide 项目
  自带的副本）拷贝取得，未做任何修改
- 包含：SDK 头文件树 + `x32dbg.lib` / `x64dbg.lib` / `x32bridge.lib` / `x64bridge.lib`

## 使用方式

CMake 里把包含目录指到这里、按架构链接对应的 .lib，参照
`antidebug/plugin/CMakeLists.txt`：

```cmake
set(_sdk "${CMAKE_SOURCE_DIR}/third_party/x64dbg-pluginsdk")
target_include_directories(你的插件 PRIVATE "${_sdk}")
# x64 链 x64dbg.lib + x64bridge.lib；x86 链 x32dbg.lib + x32bridge.lib
```

## 许可注意

x64dbg 项目按 GPL-3.0 流转。本副本作为**构建输入**内部使用；仓库自用/内部
研究没有问题。若将来要公开分发本合集的插件，需重新评估与 GPL 的兼容方式
（见 `antidebug/docs/06-security-and-ops.md` §6）。升级 SDK 时：从对应版本的
x64dbg 发行包里取 `pluginsdk/` 整目录覆盖本目录即可。
