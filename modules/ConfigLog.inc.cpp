// ========== 配置 ==========

static const size_t CONFIG_TEXT_BUFFER_SIZE = 2 * 1024; // INI 字符串配置缓冲：2KB。

static struct Config {
    char* bg_file;          // BackgroundPcx 文件名，相对于插件目录 pcx 子目录，堆分配。
    // 布局参数（从 INI 读取，方便调整）
    int   shift;            // 元素下移量（默认13）
    int   btn_margin_bottom;// 确认按钮距窗口底部偏移（原硬编码40）
    int   dismiss_btn_margin_bottom;  // 解雇按钮距窗口底部偏移（默认90）
    int   spell_btn_margin_bottom;    // 魔法书/施法按钮距窗口底部偏移（默认132）
    int   desc_y;           // 描述文字框 Y 坐标（原硬编码232+SHIFT=248）
    int   text_height;      // 描述文本区高度（默认207，原版105）
    int   text_width;       // 描述文本区宽度（exe patch 值，原0xC8=200）
    int   info_bar_margin_bottom; // 详细信息栏距窗口底部偏移（原26）
    int   window_height;    // 窗口高度（exe patch 值，原0x1E7=487）
    int   desc_x_offset;   // 描述文字水平偏移（正值右移，原0，默认5）
} cfg;

static char g_ini_path[MAX_PATH];
static char g_log_path[MAX_PATH];
static wchar_t g_log_path_w[MAX_PATH * 2];
static HMODULE g_hModule = nullptr;
static char g_default_bg_file[] = "bv_bgA.pcx";
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
    // 布局参数
    cfg.shift                  = GetPrivateProfileIntA("Layout", "Shift",             13,  f);
    cfg.btn_margin_bottom      = GetPrivateProfileIntA("Layout", "BtnMarginBottom",   40,  f);
    cfg.dismiss_btn_margin_bottom = GetPrivateProfileIntA("Layout", "DismissBtnMarginBottom", 90, f);
    cfg.spell_btn_margin_bottom   = GetPrivateProfileIntA("Layout", "SpellBtnMarginBottom",  132, f);
    cfg.desc_y                 = GetPrivateProfileIntA("Layout", "DescY",             248, f);
    cfg.text_height            = GetPrivateProfileIntA("Layout", "TextHeight",        207, f);
    cfg.text_width             = GetPrivateProfileIntA("Layout", "TextWidth",         200, f);
    cfg.info_bar_margin_bottom = GetPrivateProfileIntA("Layout", "InfoBarMarginBottom",26, f);
    cfg.window_height          = GetPrivateProfileIntA("Layout", "WindowHeight",      487, f);
    cfg.desc_x_offset          = GetPrivateProfileIntA("Layout", "DescXOffset",         5, f);
}
