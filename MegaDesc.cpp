// MegaDesc.cpp
// 英雄无敌3 SoD 插件：生物信息窗口扩展（窗口加高、背景替换、按钮替换、描述区扩展）。
// 目标版本：Shadow of Death（SOD = 0xFFFFE403），仅 x86。
// Hook 地址均为原版 SoD 地址；当前在 HotA + SoD_SP 环境验证。

#define _H3API_PATCHER_X86_
#include <H3API.hpp>
#include <stdarg.h>
#include <wchar.h>
#include <stdint.h>

using namespace h3;

// 兼容旧 homm3.h 名称：项目仍使用原版窗口布局偏移，但基础类型来自 H3API.hpp。
using _Pcx8_ = H3LoadedPcx;
using _EventMsg_ = H3Msg;

// _Dlg_ 兼容 shim：代码通过 dlg->width / dlg->height 访问，
// H3BaseDlg 成员 widthDlg/heightDlg 在 +0x20/+0x24（protected），
// 而旧 _Dlg_ 的 width/height 也在 +0x20/+0x24。用 struct 别名直接 reinterpret_cast。
struct _Dlg_ {
    char _pad0[0x20];
    int width;
    int height;
};

namespace h3 {
    namespace H3Internal {
        inline int& _o_ScreenHeight() { return *reinterpret_cast<int*>(0x68C8C8); }
    }
}
#define o_ScreenHeight (h3::H3Internal::_o_ScreenHeight())

Patcher*         _P  = nullptr;
PatcherInstance* _PI = nullptr;

// 模块按顺序包含到同一个翻译单元，保证 patcher 全局对象和静态辅助函数共享同一份状态。
#include "modules/ConfigLog.inc.cpp"
#include "modules/CreatureDialog.inc.cpp"
#include "modules/PatchesAndImages.inc.cpp"
#include "modules/Entry.inc.cpp"
