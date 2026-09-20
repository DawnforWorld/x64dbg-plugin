/* SPDX-License-Identifier: MIT */
/* pluginmain.cpp —— 插件导出入口（docs/03 §8.2）。 */
#include "pluginmain.h"

#include "plugin.h"

int pluginHandle;
int hMenu;

PLUG_EXPORT bool pluginit(PLUG_INITSTRUCT *initStruct) {
    initStruct->pluginVersion = PLUGIN_VERSION;
    initStruct->sdkVersion = PLUG_SDKVERSION;
    strncpy_s(initStruct->pluginName, PLUGIN_NAME, _TRUNCATE);
    pluginHandle = initStruct->pluginHandle;
    AntiDebugInit(initStruct);
    return true;
}

PLUG_EXPORT bool plugstop() {
    AntiDebugStop();
    return true;
}

PLUG_EXPORT void plugsetup(PLUG_SETUPSTRUCT *setupStruct) {
    hMenu = setupStruct->hMenu;
    AntiDebugSetup(setupStruct);
}
