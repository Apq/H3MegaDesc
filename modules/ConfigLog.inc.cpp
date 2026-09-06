// ========== 配置 ==========

static const size_t CONFIG_TEXT_BUFFER_SIZE = 2 * 1024; // INI 字符串配置缓冲：2KB。

static struct Config {
    char* bg_file;          // BackgroundPcx 文件名，相对于插件目录 pcx 子目录，堆分配。
    char  label_fight_value[64]; // 生物信息窗口第二行标签。
    bool  enable_right_click_scroll; // 启用右键魔法描述框滚轮翻页，默认关闭。
    bool  enable_text_color_fix;      // 启用滚动文本跨行颜色补丁，默认关闭。
    // 布局参数（从 INI 读取，方便调整）
    int   shift;            // 元素下移量（默认13）
    int   confirm_btn_margin_left;    // 确认按钮金框左上角距窗口左侧（默认215）
    int   confirm_btn_margin_bottom;  // 确认按钮金框左上角距窗口底部（默认72）
    int   dismiss_btn_margin_left;    // 解雇按钮金框左上角距窗口左侧（默认215）
    int   dismiss_btn_margin_bottom;  // 解雇按钮金框左上角距窗口底部（默认122）
    int   spell_btn_margin_left;      // 魔法书按钮金框左上角距窗口左侧（默认215）
    int   spell_btn_margin_bottom;    // 魔法书按钮金框左上角距窗口底部（默认122）
    int   desc_y;           // 描述文字框 Y 坐标（原硬编码232+SHIFT=248）
    int   text_height;      // 描述文本区高度（默认105，适配298×383底图）
    int   text_width;       // 描述文本区宽度（exe patch 值，原0xC8=200）
    int   info_bar_margin_bottom; // 详细信息栏距窗口底部偏移（原26）
    int   window_height;    // 窗口高度（exe patch 值，默认383；原版311）
    int   desc_x_offset;   // 描述文字水平偏移（正值右移，原0，默认5）
    int   fight_value_y_offset; // 战斗价值行相对名称行的 Y 偏移（默认19）
    int   upgrade_btn_margin_left;    // 升级按钮左上角距窗口左侧（默认230）
    int   upgrade_btn_margin_bottom; // 升级按钮左上角距窗口底部（默认181）
} cfg;

static char g_ini_path[MAX_PATH];
static char g_log_path[MAX_PATH];
static wchar_t g_log_path_w[MAX_PATH * 2];
HMODULE g_hModule = nullptr;
static char g_default_bg_file[] = "bv_bg.pcx";
static bool g_disable_log = false;

static const int MAX_LOG_FILES_TO_KEEP = 30;
static const int MAX_LOG_FILES_TO_SCAN = 1024;

struct LogFileEntryW {
    wchar_t path[MAX_PATH * 2];
    FILETIME last_write;
};

static int __cdecl CompareLogFileEntryW(const void* a, const void* b)
{
    const LogFileEntryW* la = (const LogFileEntryW*)a;
    const LogFileEntryW* lb = (const LogFileEntryW*)b;
    int cmp = CompareFileTime(&la->last_write, &lb->last_write);
    if (cmp != 0) return cmp;
    return _wcsicmp(la->path, lb->path);
}

static char* TrimAscii(char* s)
{
    if (!s) return s;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s += 3;
    while (*s == ' ' || *s == '\t') ++s;

    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        *--end = 0;
    return s;
}

static void NormalizeUtf8ConfigStringToAnsi(char* text, int capacity)
{
    if (!text || capacity <= 1 || !text[0]) return;

    int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (wide_len <= 0 || wide_len > 256) return;

    wchar_t wide[256];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, wide_len)) return;

    BOOL used_default = FALSE;
    char ansi[256];
    int ansi_len = WideCharToMultiByte(CP_ACP, 0, wide, -1, ansi, sizeof(ansi), nullptr, &used_default);
    if (ansi_len <= 0 || used_default) return;

    strncpy(text, ansi, capacity - 1);
    text[capacity - 1] = 0;
}

static bool ReadDisableLogFromIniFileA(const char* ini_path)
{
    if (!ini_path || !ini_path[0]) return false;

    HANDLE file = CreateFileA(ini_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    char buf[4097];
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(file, buf, sizeof(buf) - 1, &bytes_read, nullptr);
    CloseHandle(file);
    if (!ok || bytes_read == 0)
        return false;
    buf[bytes_read] = 0;

    bool in_logging = false;
    char* p = buf;
    while (*p) {
        char* line = p;
        while (*p && *p != '\r' && *p != '\n') ++p;
        if (*p) {
            *p++ = 0;
            if (p[-1] == '\r' && *p == '\n') ++p;
        }

        char* s = TrimAscii(line);
        if (!s || !*s || *s == ';' || *s == '#')
            continue;

        if (*s == '[') {
            char* close = strchr(s, ']');
            if (!close) {
                in_logging = false;
                continue;
            }
            *close = 0;
            char* section = TrimAscii(s + 1);
            in_logging = section && _stricmp(section, "Logging") == 0;
            continue;
        }

        if (!in_logging)
            continue;

        char* eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = TrimAscii(s);
        char* value = TrimAscii(eq + 1);
        if (key && value && _stricmp(key, "DisableLog") == 0)
            return atoi(value) != 0;
    }

    return false;
}

static void CleanupOldLogFilesW(const wchar_t* log_dir, const wchar_t* log_base, const wchar_t* current_log_path)
{
    if (!log_dir || !log_dir[0] || !log_base || !log_base[0]) return;

    wchar_t pattern[MAX_PATH * 2];
    _snwprintf_s(pattern, _countof(pattern), _TRUNCATE, L"%s\\%s_*.log", log_dir, log_base);

    LogFileEntryW* entries = (LogFileEntryW*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_LOG_FILES_TO_SCAN * sizeof(LogFileEntryW));
    if (!entries) return;
    int count = 0;
    bool current_found = false;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (count >= MAX_LOG_FILES_TO_SCAN) break;
        _snwprintf_s(entries[count].path, _countof(entries[count].path), _TRUNCATE, L"%s\\%s", log_dir, fd.cFileName);
        entries[count].last_write = fd.ftLastWriteTime;
        if (current_log_path && _wcsicmp(entries[count].path, current_log_path) == 0) current_found = true;
        ++count;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    int keep_existing = current_found ? MAX_LOG_FILES_TO_KEEP : (MAX_LOG_FILES_TO_KEEP - 1);
    if (keep_existing < 0) keep_existing = 0;
    if (count <= keep_existing) { HeapFree(GetProcessHeap(), 0, entries); return; }

    qsort(entries, count, sizeof(entries[0]), CompareLogFileEntryW);
    int delete_count = count - keep_existing;
    for (int i = 0; i < delete_count; ++i) DeleteFileW(entries[i].path);
    HeapFree(GetProcessHeap(), 0, entries);
}

static void SetupDatedLogPathAndCleanup(HMODULE hModule)
{
    if (g_disable_log) {
        g_log_path[0] = 0;
        g_log_path_w[0] = 0;
        return;
    }

    wchar_t module_path[MAX_PATH * 2] = { 0 };
    GetModuleFileNameW(hModule, module_path, _countof(module_path));

    wchar_t dir[MAX_PATH * 2] = { 0 };
    wchar_t base[MAX_PATH * 2] = { 0 };
    const wchar_t* slash1 = wcsrchr(module_path, L'\\');
    const wchar_t* slash2 = wcsrchr(module_path, L'/');
    const wchar_t* slash = slash1 > slash2 ? slash1 : slash2;
    const wchar_t* name = slash ? slash + 1 : module_path;
    if (slash) {
        int len = (int)(slash - module_path);
        if (len >= (int)_countof(dir)) len = (int)_countof(dir) - 1;
        memcpy(dir, module_path, len * sizeof(wchar_t));
        dir[len] = 0;
    } else {
        wcscpy_s(dir, L".");
    }
    wcsncpy_s(base, name, _TRUNCATE);
    wchar_t* dot = wcsrchr(base, L'.');
    if (dot) *dot = 0;

    SYSTEMTIME st;
    GetLocalTime(&st);
    _snwprintf_s(g_log_path_w, _countof(g_log_path_w), _TRUNCATE,
        L"%s\\%s_%04u%02u%02u_%02u%02u%02u.log",
        dir, base, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    WideCharToMultiByte(CP_UTF8, 0, g_log_path_w, -1, g_log_path, sizeof(g_log_path), nullptr, nullptr);
    CleanupOldLogFilesW(dir, base, g_log_path_w);
    // 直接追加日志行（WriteLog 此时尚不可见，用原始文件 API）
    HANDLE hf = CreateFileW(g_log_path_w, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER fpos; fpos.QuadPart = 0;
        if (SetFilePointerEx(hf, fpos, &fpos, FILE_END) && fpos.QuadPart == 0) {
            DWORD wr; WriteFile(hf, "\xEF\xBB\xBF", 3, &wr, nullptr);
        }
        SYSTEMTIME st; GetLocalTime(&st);
        char line[128];
        int n = _snprintf(line, sizeof(line)-1, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] 旧日志清理完成。\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        if (n > 0) { DWORD wr; WriteFile(hf, line, (DWORD)n, &wr, nullptr); }
        CloseHandle(hf);
    }
}


static void EnsureConfigBuffers()
{
    if (!cfg.bg_file) {
        cfg.bg_file = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, CONFIG_TEXT_BUFFER_SIZE);
        if (cfg.bg_file) lstrcpynA(cfg.bg_file, g_default_bg_file, (int)CONFIG_TEXT_BUFFER_SIZE);
    }
}

static void ReadConfig()
{
    EnsureConfigBuffers();
    if (!cfg.bg_file) cfg.bg_file = g_default_bg_file;

    const char* f = g_ini_path;
    GetPrivateProfileStringA("Images", "BackgroundPcx", g_default_bg_file, cfg.bg_file, (DWORD)CONFIG_TEXT_BUFFER_SIZE, f);
    if (!cfg.bg_file[0]) lstrcpynA(cfg.bg_file, g_default_bg_file, (int)CONFIG_TEXT_BUFFER_SIZE);
    GetPrivateProfileStringA("Format", "LabelFightValue", "Fight Value", cfg.label_fight_value, sizeof(cfg.label_fight_value), f);
    NormalizeUtf8ConfigStringToAnsi(cfg.label_fight_value, sizeof(cfg.label_fight_value));
    cfg.enable_right_click_scroll = GetPrivateProfileIntA("Features", "EnableRightClickScroll", 0, f) != 0;
    cfg.enable_text_color_fix      = GetPrivateProfileIntA("Features", "EnableTextColorFix",      0, f) != 0;
    // 布局参数
    cfg.shift                  = GetPrivateProfileIntA("Layout", "Shift",             13,  f);
    cfg.confirm_btn_margin_left   = GetPrivateProfileIntA("Layout", "ConfirmBtnMarginLeft",   215, f);
    cfg.confirm_btn_margin_bottom = GetPrivateProfileIntA("Layout", "ConfirmBtnMarginBottom",  72, f);
    cfg.dismiss_btn_margin_left   = GetPrivateProfileIntA("Layout", "DismissBtnMarginLeft",   215, f);
    cfg.dismiss_btn_margin_bottom = GetPrivateProfileIntA("Layout", "DismissBtnMarginBottom", 122, f);
    cfg.spell_btn_margin_left     = GetPrivateProfileIntA("Layout", "SpellBtnMarginLeft",     215, f);
    cfg.spell_btn_margin_bottom   = GetPrivateProfileIntA("Layout", "SpellBtnMarginBottom",   122, f);
    cfg.desc_y                 = GetPrivateProfileIntA("Layout", "DescY",             248, f);
    cfg.text_height            = GetPrivateProfileIntA("Layout", "TextHeight",        105, f);
    cfg.text_width             = GetPrivateProfileIntA("Layout", "TextWidth",         200, f);
    cfg.info_bar_margin_bottom = GetPrivateProfileIntA("Layout", "InfoBarMarginBottom",26, f);
    cfg.window_height          = GetPrivateProfileIntA("Layout", "WindowHeight",      383, f);
    cfg.desc_x_offset          = GetPrivateProfileIntA("Layout", "DescXOffset",         5, f);
    cfg.fight_value_y_offset   = GetPrivateProfileIntA("Layout", "FightValueYOffset",  19, f);
    cfg.upgrade_btn_margin_left   = GetPrivateProfileIntA("Layout", "UpgradeBtnMarginLeft",   230, f);
    cfg.upgrade_btn_margin_bottom = GetPrivateProfileIntA("Layout", "UpgradeBtnMarginBottom", 181, f);
    if (cfg.fight_value_y_offset < -40) cfg.fight_value_y_offset = -40;
    if (cfg.fight_value_y_offset > 80) cfg.fight_value_y_offset = 80;
    if (cfg.confirm_btn_margin_left < 0) cfg.confirm_btn_margin_left = 0;
    if (cfg.confirm_btn_margin_left > 298) cfg.confirm_btn_margin_left = 298;
    if (cfg.confirm_btn_margin_bottom < 0) cfg.confirm_btn_margin_bottom = 0;
    if (cfg.confirm_btn_margin_bottom > cfg.window_height) cfg.confirm_btn_margin_bottom = cfg.window_height;
    if (cfg.dismiss_btn_margin_left < 0) cfg.dismiss_btn_margin_left = 0;
    if (cfg.dismiss_btn_margin_left > 298) cfg.dismiss_btn_margin_left = 298;
    if (cfg.dismiss_btn_margin_bottom < 0) cfg.dismiss_btn_margin_bottom = 0;
    if (cfg.dismiss_btn_margin_bottom > cfg.window_height) cfg.dismiss_btn_margin_bottom = cfg.window_height;
    if (cfg.spell_btn_margin_left < 0) cfg.spell_btn_margin_left = 0;
    if (cfg.spell_btn_margin_left > 298) cfg.spell_btn_margin_left = 298;
    if (cfg.spell_btn_margin_bottom < 0) cfg.spell_btn_margin_bottom = 0;
    if (cfg.spell_btn_margin_bottom > cfg.window_height) cfg.spell_btn_margin_bottom = cfg.window_height;
    if (cfg.upgrade_btn_margin_left < 0) cfg.upgrade_btn_margin_left = 0;
    if (cfg.upgrade_btn_margin_left > 298) cfg.upgrade_btn_margin_left = 298;
    if (cfg.upgrade_btn_margin_bottom < 0) cfg.upgrade_btn_margin_bottom = 0;
    if (cfg.upgrade_btn_margin_bottom > cfg.window_height) cfg.upgrade_btn_margin_bottom = cfg.window_height;
}
