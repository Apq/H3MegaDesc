// ========== 通用滚动文本鼠标滚轮补丁 ==========
// 右键弹出的魔法详情等通用对话框包含 H3DlgScrollableText 时，
// H3 原版消息循环不把 WM_MOUSEWHEEL 转发给 DefProc。
// 这里用 WH_GETMESSAGE 在线程消息泵层拦截，只接管右键弹窗创建的滚动文本；
// 左键弹窗已有原生滚动，必须保持不干预。

static HHOOK s_wheel_hook = nullptr;

// 缓存右键弹窗最近创建的 H3DlgScrollableText 指针
static char* s_last_scrollable_text = nullptr;
static DWORD s_last_right_click_time = 0;
static DWORD s_last_left_click_time = 0;

static bool IsRecentRightClickPopup()
{
    DWORD now = GetTickCount();
    return s_last_right_click_time >= s_last_left_click_time &&
           s_last_right_click_time != 0 &&
           now - s_last_right_click_time < 2000;
}

// Hook H3DlgScrollableText::Create (0x5BA360) 返回后，eax = 新对象指针
static int __stdcall Hook_CreateScrollableText(LoHook* /*h*/, HookContext* c)
{
    if (IsRecentRightClickPopup())
        s_last_scrollable_text = (char*)c->eax;
    else
        s_last_scrollable_text = nullptr;
    return EXEC_DEFAULT;
}

static void ProcessWheelForScrollableText(char* st, int wheel_delta)
{
    if (!st) return;
    __try {
        char* scroll_bar = *(char**)(st + 0x54);
        if (!scroll_bar) return;

        int tick_count = *(int*)(scroll_bar + 0x48);
        if (tick_count < 2) return;

        int tick = *(int*)(scroll_bar + 0x3C);
        int direction = (wheel_delta < 0) ? 1 : -1;
        int new_tick = tick + direction;
        if (new_tick < 0) new_tick = 0;
        if (new_tick >= tick_count) new_tick = tick_count - 1;
        if (new_tick == tick) return;

        // SetTick 更新 tick 和滑块位置数据
        THISCALL_2(void, 0x5964D0, scroll_bar, new_tick);
        // 设置 dirty flag（模仿 FUN_00596520 的行为：渲染前设、渲染后清）
        *(unsigned char*)(scroll_bar + 0x16) |= 1;

        // 重绘滚动条（滑块）。0x596F40 检查 dirty flag 决定是否画滑块。
        __try {
            FASTCALL_1(void, 0x596F40, scroll_bar);
            // 清除 dirty flag（渲染完成后）
            *(unsigned char*)(scroll_bar + 0x16) &= (unsigned char)~1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            WriteLog("[Wheel] 596F40 异常 code=0x%08X", GetExceptionCode());
        }

        // vScrollCallOwner 最后调用：通知文本控件更新内容。
        // 放在渲染之后，避免回调二次修改 tick 导致跳跃。
        void** sb_vt = *(void***)scroll_bar;
        if (sb_vt && sb_vt[16]) {
            THISCALL_1(void, (DWORD)sb_vt[16], scroll_bar);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[Wheel] ProcessWheel 异常 code=0x%08X", GetExceptionCode());
    }
}

static LRESULT CALLBACK WheelGetMsgProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && wParam == PM_REMOVE) {
        MSG* m = (MSG*)lParam;
        if (m->message == WM_RBUTTONDOWN || m->message == WM_RBUTTONUP) {
            s_last_right_click_time = GetTickCount();
        } else if (m->message == WM_LBUTTONDOWN || m->message == WM_LBUTTONUP) {
            s_last_left_click_time = GetTickCount();
            s_last_scrollable_text = nullptr;
        } else if (m->message == WM_MOUSEWHEEL) {
            short delta = (short)HIWORD(m->wParam);
            ProcessWheelForScrollableText(s_last_scrollable_text, delta);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
