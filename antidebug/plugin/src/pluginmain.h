/* SPDX-License-Identifier: MIT */
/* pluginmain.h —— x64dbg 插件导出入口（SDK v1，骨架参考 TitanHide_x64dbg）。 */
#pragma once

/* 顺序敏感：bridgemain.h 先引入 <windows.h>，_plugins.h 里的 dbghelp
 * 才能解析（TitanHide 同款顺序） */
#include "bridgemain.h"
#include "_plugins.h"

#define PLUGIN_NAME "AntiDebug"
#define PLUGIN_VERSION 1

#define PLUG_EXPORT extern "C" __declspec(dllexport)

/* superglobal */
extern int pluginHandle;
extern int hMenu;  /* 本插件在"插件"菜单下的子菜单句柄 */

/* plugin.cpp 提供 */
void AntiDebugInit(PLUG_INITSTRUCT *initStruct);
void AntiDebugSetup(PLUG_SETUPSTRUCT *setupStruct);
void AntiDebugStop();
