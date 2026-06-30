// MegaDesc.cpp
// 英雄无敌3 SoD 插件：生物信息窗口扩展（窗口加高、背景替换、按钮替换、描述区扩展）。
// 目标版本：Shadow of Death（SOD = 0xFFFFE403），仅 x86。
// Hook 地址均为原版 SoD 地址；当前在 HotA + SoD_SP 环境验证。

#include "homm3.h"
#include <stdarg.h>
#include <wchar.h>

Patcher*         _P  = nullptr;
PatcherInstance* _PI = nullptr;

// 模块按顺序包含到同一个翻译单元，保证 patcher 全局对象和静态辅助函数共享同一份状态。
#include "modules/ConfigLog.inc.cpp"
#include "modules/CreatureDialog.inc.cpp"
#include "modules/PatchesAndImages.inc.cpp"
#include "modules/Entry.inc.cpp"
