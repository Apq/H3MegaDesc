// ========== 生物信息窗口布局调整 ==========
//
// 窗口被静态 patch 为 298×cfg.window_height（默认487）。背景 id=200；名称 id=203。
// exe 里头像/属性行/按钮/描述等坐标仍是基于原版高度(311)的硬编码，不跟随加高。
// AdjustCreatureInfoDlg 统一调度（在 BUILD 和 DefProc hook 中对 298×window_height 窗口执行）：
//   1) 除背景200/名称203/状态栏224/描述文本外，所有元素整体下移 shift；
//   2) 确认/解雇/魔法书按钮和描述贴到加高后布局的正确底部位置；
//   3) 窗口 Y 由 Hook_DlgInitClampY 贴边吸附，防止绘制越屏。
// 上述调整对战斗/冒险/城镇三种界面一视同仁。

static _Pcx8_*  s_bg_pcx8  = nullptr;
static _Pcx8_*  s_ok_btn[4] = { nullptr, nullptr, nullptr, nullptr };
static _Pcx8_*  s_ds_btn[4] = { nullptr, nullptr, nullptr, nullptr };
static _Pcx8_*  s_sp_btn[4] = { nullptr, nullptr, nullptr, nullptr };
static _Pcx8_* LoadPcx24QuantizedAsPcx8(const char* name, _Pcx8_* palSrc);
static _Pcx8_* LoadPcx24CompositeAsPcx8(const char* frameName, const char* iconName, _Pcx8_* palSrc);
static void ReplacePcx8ItemImage(char* item, _Pcx8_* pcx);
static int ButtonStateFromDef(char* def_item);

// 返回 dlg 内指定 id 的控件；用于判断窗口类型。
static char* FindDlgItem(_Dlg_* dlg, short target_id)
{
    if (!dlg) return nullptr;
    unsigned* vec = (unsigned*)((char*)dlg + 0x30);
    char** data = (char**)vec[1];
    size_t cnt = (vec[2] >= vec[1]) ? ((size_t)(vec[2] - vec[1]) / 4) : 0;
    for (size_t i = 0; i < cnt; i++) {
        char* it = data[i];
        if (it && *(short*)(it + 0x10) == target_id) return it;
    }
    return nullptr;
}

static void WriteWideFileUtf8BomIfEmpty(HANDLE h)
{
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    if (SetFilePointerEx(h, pos, &pos, FILE_END) && pos.QuadPart == 0) {
        DWORD written = 0;
        const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        WriteFile(h, bom, 3, &written, nullptr);
    }
}

static void AppendUtf8LogLine(const char* text)
{
    if (!g_log_path_w[0]) return;
    HANDLE h = CreateFileW(g_log_path_w, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteWideFileUtf8BomIfEmpty(h);
    DWORD written = 0;
    WriteFile(h, text, (DWORD)strlen(text), &written, nullptr);
    WriteFile(h, "\r\n", 2, &written, nullptr);
    CloseHandle(h);
}

static void WriteLog(const char* fmt, ...)
{
    char line[1024];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int off = _snprintf(line, sizeof(line) - 1, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (off < 0) off = 0;
    if (off >= (int)sizeof(line)) off = (int)sizeof(line) - 1;

    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line + off, sizeof(line) - off - 1, fmt, ap);
    va_end(ap);
    line[sizeof(line) - 1] = 0;
    AppendUtf8LogLine(line);
}

// ===== 按钮替换：PCX8 合成图 + DEF vtable 改写 =====
// 每个按钮（确认/解雇/魔法书）由两个控件组成：PCX8 外框（金框底图）+ DEF 本体（状态机/点击命中）。
// PCX 静态 Draw 和 DEF Draw 都不缩放，因此需要预合成目标尺寸的 PCX8 图。
// 做法：
//   1) 预加载 4 帧 PCX8 合成图（金框+图标，按按钮尺寸合成），整局缓存；
//   2) 替换 PCX8 外框控件的图像指针，同步尺寸和位置；
//   3) 复制 DEF 控件 vtable，把 Draw 槽替换为绑定 PCX8 重绘函数；
//   4) DEF 仍负责按下/高亮状态机和点击命中，视觉由 PCX8 外框控件显示。

static void* s_ok_def_novtbl[48];
static void* s_ds_def_novtbl[48];
static void* s_sp_def_novtbl[48];

struct ButtonVisualBinding {
    char* def_item;
    char* frame_item;
    _Pcx8_** states;
};

static ButtonVisualBinding s_ok_binding = { nullptr, nullptr, nullptr };
static ButtonVisualBinding s_ds_binding = { nullptr, nullptr, nullptr };
static ButtonVisualBinding s_sp_binding = { nullptr, nullptr, nullptr };

static void DrawBoundButtonPcx8(char* def_item)
{
    ButtonVisualBinding* b = nullptr;
    if (def_item == s_ok_binding.def_item) b = &s_ok_binding;
    else if (def_item == s_ds_binding.def_item) b = &s_ds_binding;
    else if (def_item == s_sp_binding.def_item) b = &s_sp_binding;
    if (!b || !b->frame_item || !b->states || !b->states[0]) return;

    int st = ButtonStateFromDef(def_item);
    if (st < 0 || st > 3 || !b->states[st]) st = 0;
    ReplacePcx8ItemImage(b->frame_item, b->states[st]);

    void** fvt = *(void***)b->frame_item;
    if (fvt && fvt[4]) {
        ((void(__fastcall*)(void*, void*))fvt[4])(b->frame_item, nullptr);
    }
}

// 解雇/确认/魔法书 DEF 的绘制：不画旧 DEF 图标；改为按 DEF 当前帧即时重画绑定的 PCX8 合成按钮。
// 这样 DEF 仍负责按下/高亮状态机和点击命中，视觉由 PCX8 外框控件显示。
static void __fastcall ButtonDefDraw(void* self, void* /*edx*/) { DrawBoundButtonPcx8((char*)self); }

static void SetDefNoDraw(char* def_item, void** novtbl)
{
    if (!def_item || !novtbl) return;
    void** dvt = *(void***)def_item;
    if (dvt != novtbl) {
        // _DlgItem_ vtable 实际有 15 个槽位，只复制这 15 个，避免越界读到垃圾/零值。
        for (int i = 0; i < 15; i++) novtbl[i] = dvt[i];
        novtbl[4] = (void*)&ButtonDefDraw;  // Draw 槽改为绘制绑定 PCX8 按钮
        *(void***)def_item = novtbl;
    }
}

static void ReplacePcx8ItemImage(char* item, _Pcx8_* pcx)
{
    if (!item || !pcx) return;
    // 替换控件内部的 _Pcx8_* 指针和尺寸字段。
    *(_Pcx8_**)(item + 0x30) = pcx;
    *(unsigned short*)(item + 0x1C) = (unsigned short)pcx->width;
    *(unsigned short*)(item + 0x1E) = (unsigned short)pcx->height;
    *(unsigned short*)(item + 0x14) &= (unsigned short)~0x10;
}

static int ButtonStateFromDef(char* def_item)
{
    if (!def_item) return 0;

    // PCX 四态编号与 DEF 帧号一一对应：
    //   0=normal, 1=pressed, 2=highlight(预留), 3=disabled
    // 按钮帧号由游戏 DEF 资源决定（通常 normal=0, pressed=1）。
    unsigned short item_state = *(unsigned short*)(def_item + 0x16);
    // H3API: IsEnabled() 返回 !(state & 0x20)，即有 0x20 才是禁用。
    if (item_state & 0x20) return 3; // 禁用
    // PRESSED 位 = 0x01：按下时使用 press_def_frame_index。
    if (item_state & 0x01) {
        int press_frame = *(int*)(def_item + 0x38);
        if (press_frame >= 0 && press_frame <= 3) return press_frame;
        return 1; // 默认按下帧
    }
    // 普通状态：使用 def_frame_index。
    int frame = *(int*)(def_item + 0x34);
    if (frame >= 0 && frame <= 3) return frame;
    return 0;
}

static void UpdatePcx8ButtonFromDef(char* frame_item, char* def_item, _Pcx8_** states, int margin_bottom, _Dlg_* dlg, void** novtbl, ButtonVisualBinding* binding)
{
    if (!frame_item || !states || !states[0] || !dlg) return;
    int st = ButtonStateFromDef(def_item);
    if (!states[st]) st = 0;
    _Pcx8_* img = states[st];

    // 统一固定到右侧按钮列。不要再根据外框旧 X 推算；
    // 紫龙魔法书场景里旧外框可能仍在左侧，会把新按钮也推回左侧。
    short new_w = (short)img->width;
    short new_h = (short)img->height;
    short new_x = (short)(281 - new_w); // 右边缘对齐原确认按钮右边界 x=215,w=66 → 281
    short new_y = (short)(dlg->height - new_h - margin_bottom);

    *(short*)(frame_item + 0x18) = new_x;
    *(short*)(frame_item + 0x1A) = new_y;

    // 替换 PCX8 图像并同步外框控件尺寸。
    ReplacePcx8ItemImage(frame_item, img);

    // DEF 只保留状态机/点击命中；命中框跟新按钮图同步。
    if (def_item) {
        *(short*)(def_item + 0x18) = new_x;
        *(short*)(def_item + 0x1A) = new_y;
        *(unsigned short*)(def_item + 0x1C) = (unsigned short)new_w;
        *(unsigned short*)(def_item + 0x1E) = (unsigned short)new_h;
    }
    if (binding) {
        binding->def_item = def_item;
        binding->frame_item = frame_item;
        binding->states = states;
    }
    SetDefNoDraw(def_item, novtbl);
}

// 在原版 _DlgStaticText_::Create 调用前改写参数栈。
// 原版 height 是 push imm8，TextHeight=207 会被 6A CF 符号扩展成 -49/0xFFCF。
// 因此不能只靠原地 hex patch；在 call 0x5BC6A0 前把栈上的 x/y/w/h 改成 32-bit 正值。
static int __stdcall Hook_DescTextCreateParams(LoHook* /*h*/, HookContext* c)
{
    int* sp = (int*)c->esp;
    __try {
        sp[0] = 20 + cfg.desc_x_offset;   // x
        sp[1] = cfg.desc_y + 6;     // y
        sp[2] = cfg.text_width;     // width
        sp[3] = cfg.text_height;    // height, true 32-bit value (e.g. 207)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return EXEC_DEFAULT;
}

static int GetSafeRenderHeight()
{
    static int cached = -1;
    if (cached > 0) return cached;

    int fallback_h = o_ScreenHeight;
    int hd_h = 0;

    // 实际绘制 buffer 高度优先读取 HD Mod 启动器配置。
    // o_ScreenHeight 在 HD/OpenGL 模式下可能是桌面/逻辑高度（例如 1440），不能用于防越界。
    char path[MAX_PATH] = { 0 };
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        char* slash = strrchr(path, '\\');
        if (slash) {
            slash[1] = 0;
            strncat(path, "_HD3_Data\\Settings\\sod.ini", MAX_PATH - strlen(path) - 1);
            HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                DWORD size = GetFileSize(f, nullptr);
                if (size > 0 && size < 1024 * 1024) {
                    char* buf = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 1);
                    if (buf) {
                        DWORD rd = 0;
                        if (ReadFile(f, buf, size, &rd, nullptr) && rd > 0) {
                            buf[rd] = 0;
                            char* p = strstr(buf, "<Graphics.Resolution>");
                            if (p) {
                                int w = 0, hh = 0;
                                char* eq = strchr(p, '=');
                                if (eq) {
                                    if (sscanf_s(eq + 1, " %d x %d", &w, &hh) != 2)
                                        sscanf_s(eq + 1, " %d , %d", &w, &hh);
                                    if (w > 0 && hh > 0) hd_h = hh;
                                }
                            }
                        }
                        HeapFree(GetProcessHeap(), 0, buf);
                    }
                }
                CloseHandle(f);
            }
        }
    }

    cached = (hd_h > 0) ? hd_h : fallback_h;
    return cached;
}

static int ClampCreatureDlgY(int old_y)
{
    int sy = GetSafeRenderHeight();
    if (sy <= 0) return old_y;

    // 两个约束都要满足：
    // 1) 整个加高后的窗口不能超出屏幕；
    // 2) 描述区域绝对底边不能超出绘制 buffer。
    int max_by_window = sy - cfg.window_height;
    int max_by_desc = sy - (cfg.desc_y + 6) - cfg.text_height;
    int max_y = (max_by_window < max_by_desc) ? max_by_window : max_by_desc;
    if (max_y < 0) max_y = 0;
    if (old_y > max_y) return max_y;
    if (old_y < 0) return 0;
    return old_y;
}

static void PatchCreatureDlgYParam(HookContext* c, int y_offset)
{
    __try {
        int* py = (int*)(c->ebp + y_offset);
        int old_y = *py;
        int new_y = ClampCreatureDlgY(old_y);
        if (new_y != old_y) {
            *py = new_y;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// 冒险/城镇构造函数：0041AFA0 前参数为 [ebp+0C]=x, [ebp+10]=y。
static int __stdcall Hook_CreatureDlgY_Ebp10(LoHook* /*h*/, HookContext* c)
{
    PatchCreatureDlgYParam(c, 0x10);
    return EXEC_DEFAULT;
}

// 战斗构造函数：0041AFA0 前参数为 [ebp+18]=x, [ebp+1C]=y。
static int __stdcall Hook_CreatureDlgY_Ebp1C(LoHook* /*h*/, HookContext* c)
{
    PatchCreatureDlgYParam(c, 0x1C);
    return EXEC_DEFAULT;
}

// 更底层的窗口初始化统一吸附：_Dlg_ 初始化函数 0x41AFA0。
// thiscall，调用点参数顺序为 push flags, h, w, y, x；函数入口栈为 [ret, x, y, w, h, flags]。
// 只处理生物信息窗口尺寸，覆盖右键临时窗口/HD 包装等未命中上层构造 hook 的路径。
static int __stdcall Hook_DlgInitClampY(LoHook* /*h*/, HookContext* c)
{
    __try {
        int* sp = (int*)c->esp;
        // LoHook 入口的 esp 布局在不同 hook/包装下可能包含原 ret，也可能已经跳过 ret。
        // 扫描前几个 dword，寻找连续 x,y,w,h，其中 w=298 且 h=cfg.window_height。
        for (int base = 0; base <= 5; ++base) {
            int old_y = sp[base + 1];
            int w = sp[base + 2];
            int hgt = sp[base + 3];
            if (w == 298 && hgt == cfg.window_height) {
                int new_y = ClampCreatureDlgY(old_y);
                if (new_y != old_y) {
                    sp[base + 1] = new_y;
                }
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return EXEC_DEFAULT;
}

// 对一个 298×window_height 生物信息窗口执行通用布局调整：元素下移 + 按钮替换 + 描述修正。
static void AdjustCreatureInfoDlg(_Dlg_* dlg)
{
    if (!dlg || dlg->width != 298 || dlg->height != cfg.window_height) return;
    if (!FindDlgItem(dlg, 200)) return;  // 不是生物信息窗口，跳过

    unsigned* vec = (unsigned*)((char*)dlg + 0x30);
    char** data = (char**)vec[1];                                   // Data@+4
    size_t cnt = (vec[2] >= vec[1]) ? ((size_t)(vec[2] - vec[1]) / 4) : 0;

    // 解雇按钮仅在"双击管理窗口"出现；魔法书按钮仅在法术生物出现。
    bool has_dismiss = (FindDlgItem(dlg, 30723) != nullptr);
    bool has_spell   = (FindDlgItem(dlg, 30724) != nullptr) || (FindDlgItem(dlg, 301) != nullptr);
    char* old_bg = nullptr;        // 原背景控件 id=200，用它的 z_order 保持原遮挡关系
    char* ok_frame = nullptr;      // 确认按钮 PCX8 外框/底图控件(id=225)
    char* ok_def   = nullptr;      // 确认按钮 DEF 控件(id=30722)
    char* dismiss_frame = nullptr; // 解雇按钮 PCX8 外框/底图控件(id=-1,小)
    char* dismiss_def   = nullptr; // 解雇 DEF 控件(id=30723)
    char* spell_frame = nullptr;   // 魔法书按钮 PCX8 外框/底图控件(id=-1,小)
    char* spell_def   = nullptr;   // 魔法书 DEF 控件(id=30724)
    char* desc_item   = nullptr;   // 描述文字框：原版为 id=-1；补创建时使用独立 id=3010
    int   minus1_small  = 0;       // id=-1 且 ih<100 的控件计数（区分解雇金框和魔法书金框）
    char* small_frames[8] = { 0 };
    short small_frame_x[8] = { 0 };
    short small_frame_y[8] = { 0 };
    int small_frame_count = 0;
    short spell_def_old_x = -32768;
    short spell_def_old_y = -32768;

    // 是否需要下移：以"本次进程内是否已处理过该 dlg 指针"为主判断，避免每帧重复累加。
    // 生物信息窗口是模态的、同一时间只有一个；新窗口(指针不同)首次进入时下移，之后跳过。
    // 这样不依赖具体 item id，战斗/冒险/城镇三种界面(元素 id 可能略有差异)都能正确触发一次。
    // 兜底：若指针命中但内容仍处原始高位(头像id=201/202 y<60)，说明地址被复用、实际是新窗口，仍下移。
    static _Dlg_* s_adjusted = nullptr;
    bool need_shift = (dlg != s_adjusted);
    if (!need_shift) {
        for (size_t i = 0; i < cnt; i++) {
            char* it = data[i];
            if (!it) continue;
            short id = *(short*)(it + 0x10);
            if ((id == 201 || id == 202) && *(short*)(it + 0x1A) < 60) { need_shift = true; break; }
        }
    }

    for (size_t i = 0; i < cnt; i++) {
        char* it = data[i];
        if (!it) continue;
        short id = *(short*)(it + 0x10);
        unsigned short ih = *(unsigned short*)(it + 0x1E);
        unsigned short flags = *(unsigned short*)(it + 0x14);
        void** vt = *(void***)it;
        short y = *(short*)(it + 0x1A);

        // 先记录所有 id=-1 小金框的原始位置，后面用 DEF 原始坐标匹配真正的魔法书金框。
        if (id == -1 && ih < 100 && small_frame_count < 8) {
            small_frames[small_frame_count] = it;
            small_frame_x[small_frame_count] = *(short*)(it + 0x18);
            small_frame_y[small_frame_count] = *(short*)(it + 0x1A);
            small_frame_count++;
        }

        if (id == 200) old_bg = it;

        bool is_minus1_text = (id == -1 && (flags & 0x8) && (vt == (void**)0x642DC0 || vt == (void**)0x642DF8));

    // 元素下移（只执行一次）：除背景200/名称203/状态栏224/描述文本外整体 +SHIFT
        if (need_shift && id != 200 && id != 203 && id != 224 && !is_minus1_text) *(short*)(it + 0x1A) = (short)(y + cfg.shift);

        // 确认按钮外框(66×32)：贴窗口底，距底 cfg.btn_margin_bottom。
        if (id == 225) {
            *(short*)(it + 0x1A) = (short)(dlg->height - ih - cfg.btn_margin_bottom);
            ok_frame = it;
        } else if (id == 30722) {
            // 确认按钮 DEF 本体：后续仅保留点击命中，绘制由 PCX8 外框控件承担。
            *(short*)(it + 0x1A) = (short)(dlg->height - 32 - cfg.btn_margin_bottom + 1);
            ok_def = it;
        } else if (id == 30723) {
            // 解雇 DEF 本体：Y 由 DismissBtnMarginBottom 控制。
            *(short*)(it + 0x18) = 215;
            *(short*)(it + 0x1A) = (short)(dlg->height - 32 - cfg.dismiss_btn_margin_bottom);
            *(unsigned short*)(it + 0x1C) = 66;
            *(unsigned short*)(it + 0x1E) = 32;
            dismiss_def = it;
        } else if (id == 30724 || id == 301) {
            // 魔法书/施法 DEF 本体：id=30724 是部分场景，id=301 是战场左键己方紫龙。
            // Y 由 SpellBtnMarginBottom 控制；后续仅保留点击命中。
            // 先记录原始位置，用于在 id=-1 小控件里匹配真正的魔法书金框。
            spell_def_old_x = *(short*)(it + 0x18);
            spell_def_old_y = *(short*)(it + 0x1A);
            *(short*)(it + 0x18) = 215;
            *(short*)(it + 0x1A) = (short)(dlg->height - 32 - cfg.spell_btn_margin_bottom);
            *(unsigned short*)(it + 0x1C) = 66;
            *(unsigned short*)(it + 0x1E) = 32;
            spell_def = it;
        } else if (id == -1 && (has_dismiss || has_spell) && ih < 100 && vt == (void**)0x63BA94) {
            // id=-1 小控件：按钮金框，必须是 PCX8 静态控件(vt=0x63BA94)。
            // 紫龙左键的原版描述也是 id=-1 且 h=41，但它是文本控件，不能被这里误判成按钮金框。
            // 可能只有解雇、只有魔法书，或两者都有；不能要求 has_dismiss 才进入。
            if (has_dismiss && minus1_small == 0) {
                *(short*)(it + 0x18) = 215;
                *(short*)(it + 0x1A) = (short)(dlg->height - 32 - cfg.dismiss_btn_margin_bottom);
                *(unsigned short*)(it + 0x1C) = 66;
                *(unsigned short*)(it + 0x1E) = 32;
                dismiss_frame = it;
            } else if (has_spell && ((has_dismiss && minus1_small == 1) || (!has_dismiss && minus1_small == 0))) {
                *(short*)(it + 0x18) = 215;
                *(short*)(it + 0x1A) = (short)(dlg->height - 32 - cfg.spell_btn_margin_bottom);
                *(unsigned short*)(it + 0x1C) = 66;
                *(unsigned short*)(it + 0x1E) = 32;
                spell_frame = it;
            }
            minus1_small++;
        } else if (id == -1 && (flags & 0x8) && (vt == (void**)0x642DC0 || vt == (void**)0x642DF8)
            && *(unsigned short*)(it + 0x1C) >= 120 && *(unsigned short*)(it + 0x1C) <= 240
            && *(short*)(it + 0x1A) >= 200 && *(short*)(it + 0x1A) <= 270) {
            // 描述文字框：正常控件（h≤120）BUILD 阶段兜底调整位置和尺寸；
            // 扩展高度控件（如 h=207）由 Hook_DescTextCreateParams 在 Create 调用前写栈，此处不改。
            unsigned short dh = *(unsigned short*)(it + 0x1E);
            short dx = *(short*)(it + 0x18);
            short dy = *(short*)(it + 0x1A);
            unsigned short dw = *(unsigned short*)(it + 0x1C);
            short target_y = (short)(cfg.desc_y + 6);
            if (dh >= 20 && dh <= 120) {
                if (dx != (short)(20 + cfg.desc_x_offset) || dy != target_y || dw != (unsigned short)cfg.text_width) {
                    *(short*)(it + 0x18) = (short)(20 + cfg.desc_x_offset);
                    *(short*)(it + 0x1A) = target_y;
                    *(unsigned short*)(it + 0x1C) = (unsigned short)cfg.text_width;
                }
            }
            desc_item = it;
        }
    }

    // 魔法书金框不能靠"第几个 id=-1 小控件"猜；紫龙左键场景只有魔法书没有解雇时顺序会变。
    // 用魔法书 DEF 原始坐标匹配最近的 id=-1 小金框，再统一移动到右侧按钮列。
    if (has_spell && spell_def && spell_def_old_x != -32768) {
        char* best = nullptr;
        int best_score = 0x7fffffff;
        for (int i = 0; i < small_frame_count; i++) {
            char* it = small_frames[i];
            if (!it || it == dismiss_frame) continue;
            int dx = (int)small_frame_x[i] - (int)spell_def_old_x;
            int dy = (int)small_frame_y[i] - (int)spell_def_old_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            int score = dx + dy;
            if (score < best_score) { best_score = score; best = it; }
        }
        if (best) {
            *(short*)(best + 0x18) = 215;
            *(short*)(best + 0x1A) = (short)(dlg->height - 32 - cfg.spell_btn_margin_bottom);
            *(unsigned short*)(best + 0x1C) = 66;
            *(unsigned short*)(best + 0x1E) = 32;
            spell_frame = best;
        }
    }

    if (need_shift) s_adjusted = dlg;

    // --- 描述文字：若原版未创建描述控件，则补创建一个独立文本控件 ---
    // 注意：不要伪装成 id=-1，也不要直接调用底层 b_DlgStaticText_Create(flags=8)；
    // 之前这样会在部分入口触发字体/文本渲染链路崩溃。
    // 这里改用已验证稳定的 _DlgStaticText_::Create 包装函数。
    if (need_shift && !desc_item && !FindDlgItem(dlg, 3010)) {
        // 若原版没有可安全移动的描述控件，则在确认是生物窗口后补创建。
        // 枯木战士也是生物；不能因为原版 id=-1 文本控件尺寸异常就排除。
        // 约束：必须有数量文本 id=204 和正常 creature_id。
        char* count_for_desc = FindDlgItem(dlg, 204);
        int creature_id_for_desc = *(int*)((char*)dlg + 0x60);
        if (count_for_desc && creature_id_for_desc >= 0 && creature_id_for_desc < 197) {
            char* table = *(char**)0x6747B0;
            if (table) {
                const char* desc_text = *(const char**)(table + creature_id_for_desc * 0x74 + 0x1C);
                if (desc_text && desc_text[0]) {
                    H3DlgText* desc = H3DlgText::Create(
                        20 + cfg.desc_x_offset, cfg.desc_y + 6, cfg.text_width, cfg.text_height,
                        (char*)desc_text, (char*)"smalfont.fnt", 4, 3010, 0, 0);
                    if (desc) {
                        reinterpret_cast<H3BaseDlg*>(dlg)->AddItem(reinterpret_cast<H3DlgItem*>(desc));
                        desc_item = (char*)desc;
                    }
                }
            }
        }
    }

    // --- 背景：走原生 PCX8 控件路径 ---
    // 把 24-bit PCX 量化到原游戏调色板后，替换旧背景控件(id=200)内部的 _Pcx8_*。
    if (need_shift && !s_bg_pcx8 && old_bg) {
        _Pcx8_* oldPcx8 = *(_Pcx8_**)(old_bg + 0x30);
        s_bg_pcx8 = LoadPcx24QuantizedAsPcx8(cfg.bg_file, oldPcx8);
        if (!s_bg_pcx8) WriteLog("背景图加载失败: %s", cfg.bg_file);
    }
    if (need_shift && s_bg_pcx8 && old_bg) {
        ReplacePcx8ItemImage(old_bg, s_bg_pcx8);
    }

    // --- 按钮图像：复用原 PCX8 外框控件，位置回到 INI 的底边距控制 ---
    if (need_shift && old_bg) {
        _Pcx8_* palSrc = *(_Pcx8_**)(old_bg + 0x30);
        if (!s_ok_btn[0]) {
            s_ok_btn[0] = LoadPcx24CompositeAsPcx8("bv_frmL.pcx", "bv_ok0.pcx", palSrc);
            s_ok_btn[1] = LoadPcx24CompositeAsPcx8("bv_frmL.pcx", "bv_ok1.pcx", palSrc);
            s_ok_btn[2] = LoadPcx24CompositeAsPcx8("bv_frmL.pcx", "bv_ok2.pcx", palSrc);
            s_ok_btn[3] = LoadPcx24CompositeAsPcx8("bv_frmL.pcx", "bv_ok3.pcx", palSrc);
        }
        if (!s_ds_btn[0]) {
            s_ds_btn[0] = LoadPcx24CompositeAsPcx8("bv_frmS.pcx", "bv_ds0.pcx", palSrc);
            s_ds_btn[1] = LoadPcx24CompositeAsPcx8("bv_frmS.pcx", "bv_ds1.pcx", palSrc);
            s_ds_btn[2] = LoadPcx24CompositeAsPcx8("bv_frmS.pcx", "bv_ds2.pcx", palSrc);
            s_ds_btn[3] = LoadPcx24CompositeAsPcx8("bv_frmS.pcx", "bv_ds3.pcx", palSrc);
        }
        if (!s_sp_btn[0]) {
            s_sp_btn[0] = LoadPcx24CompositeAsPcx8("bv_frmS.pcx", "bv_sp0.pcx", palSrc);
            s_sp_btn[1] = LoadPcx24CompositeAsPcx8("bv_frmS.pcx", "bv_sp1.pcx", palSrc);
            s_sp_btn[2] = LoadPcx24CompositeAsPcx8("bv_frmS.pcx", "bv_sp2.pcx", palSrc);
            s_sp_btn[3] = LoadPcx24CompositeAsPcx8("bv_frmS.pcx", "bv_sp3.pcx", palSrc);
        }
    }
    if (need_shift && dismiss_frame && s_ds_btn[0]) {
        UpdatePcx8ButtonFromDef(dismiss_frame, dismiss_def, s_ds_btn, cfg.dismiss_btn_margin_bottom, dlg, (void**)s_ds_def_novtbl, &s_ds_binding);
    }
    if (need_shift && spell_frame && s_sp_btn[0]) {
        UpdatePcx8ButtonFromDef(spell_frame, spell_def, s_sp_btn, cfg.spell_btn_margin_bottom, dlg, (void**)s_sp_def_novtbl, &s_sp_binding);
    }
    if (need_shift && ok_frame && s_ok_btn[0]) {
        UpdatePcx8ButtonFromDef(ok_frame, ok_def, s_ok_btn, cfg.btn_margin_bottom, dlg, (void**)s_ok_def_novtbl, &s_ok_binding);
    }

    // 注意：不在 BUILD 阶段直接修改 dlg->x/y。窗口位置由 Hook_DlgInitClampY 统一处理。
}

// BUILD-phase hooks：在窗口构建期（显示前）执行调整，hold/release 两种模式都覆盖。
// 各界面最后一个 AddItemToOwnArrayList 之后的寄存器语境：战斗 ebx=dlg；冒险 esi=dlg；城镇 esi=dlg。
int __stdcall Hook_BuildCombat(LoHook* h, HookContext* c)
{
    DoLogCleanupOnce();
    AdjustCreatureInfoDlg((_Dlg_*)c->ebx);
    return EXEC_DEFAULT;
}
int __stdcall Hook_BuildAdventure(LoHook* h, HookContext* c)
{
    DoLogCleanupOnce();
    AdjustCreatureInfoDlg((_Dlg_*)c->esi);
    return EXEC_DEFAULT;
}
int __stdcall Hook_BuildTown(LoHook* h, HookContext* c)
{
    DoLogCleanupOnce();
    AdjustCreatureInfoDlg((_Dlg_*)c->esi);
    return EXEC_DEFAULT;
}

// HiHook 0x41B120（_Dlg_::DefProc）：兜底——仅在确认为生物信息窗口时才处理。
int __stdcall Hook_DlgDefProc(HiHook* h, _Dlg_* dlg, _EventMsg_* msg)
{
    // 严格过滤：只有 298×cfg.window_height 且包含 id=200 背景控件的对话框才处理。
    // 避免存档/读档/其它对话框进入后遍历未初始化的 item 数组导致崩溃。
    if (dlg && dlg->width == 298 && dlg->height == cfg.window_height && FindDlgItem(dlg, 200)) {
        AdjustCreatureInfoDlg(dlg);
    }
    return THISCALL_2(int, h->GetDefaultFunc(), dlg, msg);
}


