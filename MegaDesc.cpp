// MegaDesc.cpp
// 英雄无敌3 SoD 插件：生物信息窗口扩展（窗口加高/背景替换/按钮替换/描述区扩展）
// 目标版本：Shadow of Death（SOD = 0xFFFFE403），仅 x86
// 注意：hook 地址均为原版 SoD 地址；当前在 HotA + SoD_SP 环境验证。

#include "homm3.h"
#include <stdarg.h>
#include <wchar.h>

Patcher*         _P  = nullptr;
PatcherInstance* _PI = nullptr;

// Implementation is split into module fragments and included in order.
// These are .inc.cpp files intentionally included by this translation unit to keep
// patcher globals/static helpers identical to the historical single-file build.
#include "modules/ConfigLog.inc.cpp"
#include "modules/CreatureDialog.inc.cpp"
#include "modules/PatchesAndImages.inc.cpp"
#include "modules/Entry.inc.cpp"
