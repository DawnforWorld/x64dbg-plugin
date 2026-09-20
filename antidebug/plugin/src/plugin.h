/* SPDX-License-Identifier: MIT */
/* plugin.h —— 插件逻辑层（菜单/命令/事件自动化）的内部接口。 */
#pragma once

#include "pluginmain.h"

/* 菜单项 ID（CB_MENUENTRY 分发用） */
enum AntiDebugMenuEntry {
    kMenuPanel = 1,
    kMenuLoad,
    kMenuStop,
    kMenuHideToggle,
    kMenuAbout,
};

/* 供命令/菜单/事件共用的动作（plugin.cpp 实现） */
bool AntiDebugActionLoad();
bool AntiDebugActionStop();
bool AntiDebugActionHide();
bool AntiDebugActionUnhide();
bool AntiDebugActionHideToggle();  /* 按当前状态切换 */
void AntiDebugActionStatus();      /* 状态打印到日志窗口 */
bool AntiDebugIsHidden();

/* 配置持久化（插件目录 antidebug_settings.ini） */
bool AntiDebugGetAutoHide();
void AntiDebugSetAutoHide(bool enabled);
uint32_t AntiDebugGetSavedTechMask();
void AntiDebugSaveTechMask(uint32_t mask);

/* 控制面板对话框（ui/settings_dialog.cpp） */
void AntiDebugShowSettingsDialog();

/* 当前被调试进程 PID（CB 事件维护） */
extern uint32_t g_debuggee_pid;
