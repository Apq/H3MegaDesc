static void StartPlugin()
{
    WriteLog("MegaDesc 开始注册 Hook。");

    // 窗口尺寸/布局 patch
    ApplyCreatureBoxPatches();

    // BUILD 阶段 hook
    _PI->WriteLoHook(0x5F4503, Hook_BuildCombat);
    _PI->WriteLoHook(0x5F3E75, Hook_BuildAdventure);
    _PI->WriteLoHook(0x5F491E, Hook_BuildTown);

    // 窗口构造期 Y 吸附：修正窗口 y，避免 ZCN2 描述绘制越屏。
    // 底层 0x41AFA0 覆盖所有路径（包括右键临时窗口等）；上层三处覆盖各构造函数入口。
    _PI->WriteLoHook(0x41AFA0, Hook_DlgInitClampY);      // _Dlg_ 初始化统一入口
    _PI->WriteLoHook(0x5F3721, Hook_CreatureDlgY_Ebp10); // 冒险构造：y=[ebp+10]
    _PI->WriteLoHook(0x5F45D1, Hook_CreatureDlgY_Ebp10); // 城镇构造：y=[ebp+10]
    _PI->WriteLoHook(0x5F3F14, Hook_CreatureDlgY_Ebp1C); // 战斗构造：y=[ebp+1C]

    // 描述控件 _DlgStaticText_::Create 调用前改写参数栈，支持 TextHeight > 127。
    _PI->WriteLoHook(0x5F447F, Hook_DescTextCreateParams); // 战斗描述文本 create call
    _PI->WriteLoHook(0x5F3E54, Hook_DescTextCreateParams); // 冒险描述文本 create call
    _PI->WriteLoHook(0x5F489A, Hook_DescTextCreateParams); // 城镇描述文本 create call

    WriteLog("MegaDesc 已启用。Hook：BUILD(战斗/冒险/城镇), DlgInitY(0x41AFA0+3), DescTextCreate(3).");
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
