static void StartPlugin()
{
    WriteLog("MegaDesc 开始注册 Hook。");

    // 窗口尺寸/布局 patch
    ApplyCreatureBoxPatches();

    // H3.TextColor 在 _DlgScrollableText_ 长文本拆行后，跨行颜色需要补回当前颜色标签。
    if (cfg.enable_text_color_fix)
        ApplyTextColorScrollableTextFix();
    else
        WriteLog("TextColor 滚动文本跨行颜色补丁已关闭。");

    // 生物信息窗口兜底：DefProc 只做布局补救，滚轮由 WH_GETMESSAGE 处理。
    _PI->WriteHiHook(0x41B120, SPLICE_, EXTENDED_, THISCALL_, Hook_DlgDefProc);

    // WH_GETMESSAGE 钩子：H3 原版消息循环不把 WM_MOUSEWHEEL 转发给 DefProc。
    // 仅接管右键弹窗创建的滚动文本，左键弹窗保留原生滚动。
    if (cfg.enable_right_click_scroll) {
        s_wheel_hook = SetWindowsHookExW(WH_GETMESSAGE, WheelGetMsgProc, g_hModule, GetCurrentThreadId());
        WriteLog("WH_GETMESSAGE 滚轮钩子 %s", s_wheel_hook ? "已安装" : "安装失败");

        // Hook H3DlgScrollableText::Create，缓存右键弹窗创建的可滚动文本控件指针
        _PI->WriteLoHook(0x5BA360, Hook_CreateScrollableText);
    } else {
        WriteLog("右键魔法描述框滚轮翻页补丁已关闭。");
    }

    // BUILD 阶段 hook；Combat/Adventure 为历史名称，实际入口以地址注释为准。
    _PI->WriteLoHook(0x5F4503, Hook_BuildCombat);    // 英雄部队，ebx=dlg
    _PI->WriteLoHook(0x5F3E75, Hook_BuildAdventure); // 战斗，esi=dlg
    _PI->WriteLoHook(0x5F491E, Hook_BuildTown);

    // 窗口构造期 Y 吸附：修正窗口 y，避免描述绘制越屏。
    // 底层 0x41AFA0 覆盖所有路径（包括右键临时窗口等）；上层三处覆盖各构造函数入口。
    _PI->WriteLoHook(0x41AFA0, Hook_DlgInitClampY);      // _Dlg_ 初始化统一入口
    _PI->WriteLoHook(0x5F3721, Hook_CreatureDlgY_Ebp10); // 战斗构造：y=[ebp+10]
    _PI->WriteLoHook(0x5F45D1, Hook_CreatureDlgY_Ebp10); // 城镇构造：y=[ebp+10]
    _PI->WriteLoHook(0x5F3F14, Hook_CreatureDlgY_Ebp1C); // 英雄部队构造：y=[ebp+1C]

    // 描述控件 _DlgStaticText_::Create 调用前改写参数栈，支持 TextHeight > 127。
    _PI->WriteLoHook(0x5F447F, Hook_DescTextCreateParams); // 英雄部队描述文本 create call
    _PI->WriteLoHook(0x5F3E54, Hook_DescTextCreateParams); // 战斗描述文本 create call
    _PI->WriteLoHook(0x5F489A, Hook_DescTextCreateParams); // 城镇描述文本 create call

    WriteLog("MegaDesc 已启用。Hook：BUILD(战斗/冒险/城镇), DlgDefProc(0x41B120), DlgInitY(0x41AFA0+3), DescTextCreate(3)；滚轮/颜色补丁按 Features 配置。");
}

// ========== DllMain ==========

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    static bool initialized = false;
    if (reason == DLL_PROCESS_ATTACH && !initialized) {
        initialized = true;
        g_hModule = hModule;
        GetModuleFileNameA(hModule, g_ini_path, MAX_PATH);
        char* dot = strrchr(g_ini_path, '.');
        if (dot) strcpy(dot, ".ini");
        g_disable_log = ReadDisableLogFromIniFileA(g_ini_path);
        SetupDatedLogPathAndCleanup(hModule);
        WriteLog("MegaDesc 正在加载。");
        _P = GetPatcher();
        if (!_P) {
            WriteLog("GetPatcher 失败；插件将保持未激活状态。");
            return TRUE;
        }
        _PI = _P->CreateInstance("HD.Plugin.MegaDesc");
        if (!_PI) {
            WriteLog("CreateInstance 失败；插件将保持未激活状态。");
            return TRUE;
        }
        ReadConfig();
        StartPlugin();
    }
    return TRUE;
}
