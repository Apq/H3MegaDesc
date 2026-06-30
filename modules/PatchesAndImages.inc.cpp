static void ApplyCreatureBoxPatches()
{
    int wh = cfg.window_height;
    char wh_hex[16]; sprintf(wh_hex, "68 %02X %02X 00 00", wh & 0xFF, (wh >> 8) & 0xFF);

    // 背景图名不改——运行时在 AdjustCreatureInfoDlg 中自己加载 PCX24 并替换控件
    // _PI->WriteHexPatch(0x68C6E8, ...

    // 窗口高度，冒险/战斗/城镇三界面各两处
    _PI->WriteHexPatch(0x5F3F1B, wh_hex);  // 战斗
    _PI->WriteHexPatch(0x5F406B, wh_hex);
    _PI->WriteHexPatch(0x5F3728, wh_hex);  // 冒险
    _PI->WriteHexPatch(0x5F38CF, wh_hex);
    _PI->WriteHexPatch(0x5F45D8, wh_hex);  // 城镇
    _PI->WriteHexPatch(0x5F4712, wh_hex);

    // 名称行参数
    _PI->WriteHexPatch(0x5F4117, "6A 13");

    // 字体改用 smalfont.fnt (0x65F2F8)
    _PI->WriteHexPatch(0x5F4469, "68 F8 F2 65 00");  // 战斗
    _PI->WriteHexPatch(0x5F3E3E, "68 F8 F2 65 00");  // 冒险
    _PI->WriteHexPatch(0x5F4884, "68 F8 F2 65 00");  // 城镇

    // 文本区宽度和创建时 Y 坐标可原地 patch。
    // 高度原位置是 push imm8，不能安全表达 >127；真正高度由 Hook_DescTextCreateParams 在 call 前写栈。
    int safe_text_height = cfg.text_height;
    if (safe_text_height > 120) safe_text_height = 105; // 原位是 push imm8；>127 会符号扩展成负高度(如 207 -> h=65487)
    if (safe_text_height < 20) safe_text_height = 20;
    char th_hex[16]; sprintf(th_hex, "6A %02X", safe_text_height & 0xFF);
    char tw_hex[16]; sprintf(tw_hex, "68 %02X %02X 00 00", cfg.text_width & 0xFF, (cfg.text_width >> 8) & 0xFF);
    int desc_create_y = cfg.desc_y + 6;
    char ty_hex[16]; sprintf(ty_hex, "68 %02X %02X 00 00", desc_create_y & 0xFF, (desc_create_y >> 8) & 0xFF);
    _PI->WriteHexPatch(0x5F446F, th_hex);           // 战斗 高度占位（真正高度由 call 前栈 hook 写入）
    _PI->WriteHexPatch(0x5F4471, tw_hex);           // 战斗 宽度
    _PI->WriteHexPatch(0x5F4476, ty_hex);           // 战斗 Y
    _PI->WriteHexPatch(0x5F3E44, th_hex);           // 冒险 高度占位
    _PI->WriteHexPatch(0x5F3E46, tw_hex);           // 冒险 宽度
    _PI->WriteHexPatch(0x5F3E4B, ty_hex);           // 冒险 Y
    _PI->WriteHexPatch(0x5F488A, th_hex);           // 城镇 高度占位
    _PI->WriteHexPatch(0x5F488C, tw_hex);           // 城镇 宽度
    _PI->WriteHexPatch(0x5F4891, ty_hex);           // 城镇 Y

    // 战斗构造函数：法术生物（紫龙）原版在创建法术面板后跳过描述创建（EB 6A → jmp 0x5F44A0）。
    // 改为 EB 05 → jmp 0x5F443B，落入描述文本检查/创建分支，使紫龙也走原版描述创建路径。
    // Hook_DescTextCreateParams（挂在 0x5F447F 的 call 前）统一改写参数，所有生物一视同仁。
    _PI->WriteHexPatch(0x5F4434, "EB 05");

    // 详细信息栏 Y 坐标 = window_height - info_bar_margin_bottom
    int info_y = wh - cfg.info_bar_margin_bottom;
    char iy_hex[16]; sprintf(iy_hex, "68 %02X %02X 00 00", info_y & 0xFF, (info_y >> 8) & 0xFF);
    _PI->WriteHexPatch(0x5F44D3, iy_hex);  // 战斗
    _PI->WriteHexPatch(0x5F3CE4, iy_hex);  // 冒险
    _PI->WriteHexPatch(0x5F48EE, iy_hex);  // 城镇
}

// ========== 图片加载器（24-bit / 3-plane PCX） ==========
// 直接读取插件目录 pcx\*.pcx，不走游戏资源系统；支持 bpp=8, planes=3 的 24-bit PCX。
// 输出 RGB888 → 量化到游戏调色板 → _Pcx8_（用于背景和按钮替换）。

static bool DecodePcx24ToRgb(const char* pcxPath, unsigned char** outRgb, int* outW, int* outH)
{
    *outRgb = nullptr; *outW = 0; *outH = 0;
    FILE* f = fopen(pcxPath, "rb");
    if (!f) return false;

    unsigned char hdr[128];
    if (fread(hdr, 1, 128, f) != 128) { fclose(f); return false; }

    int xmin = hdr[4] | (hdr[5] << 8);
    int ymin = hdr[6] | (hdr[7] << 8);
    int xmax = hdr[8] | (hdr[9] << 8);
    int ymax = hdr[10] | (hdr[11] << 8);
    int w = xmax - xmin + 1;
    int h = ymax - ymin + 1;
    int bpp = hdr[3];
    int planes = hdr[65];
    int bytesPerLine = hdr[66] | (hdr[67] << 8);

    if (hdr[0] != 0x0A || hdr[2] != 1 || bpp != 8 || planes != 3 || w <= 0 || h <= 0 || bytesPerLine < w) {
        fclose(f);
        return false;
    }

    unsigned char* rgb = (unsigned char*)malloc(w * h * 3);
    unsigned char* line = (unsigned char*)malloc(bytesPerLine * planes);
    if (!rgb || !line) { if (rgb) free(rgb); if (line) free(line); fclose(f); return false; }

    for (int y = 0; y < h; y++) {
        int need = bytesPerLine * planes;
        int pos = 0;
        while (pos < need) {
            int c = fgetc(f);
            if (c == EOF) { free(rgb); free(line); fclose(f); return false; }
            int count = 1;
            int val = c;
            if ((c & 0xC0) == 0xC0) {
                count = c & 0x3F;
                val = fgetc(f);
                if (val == EOF) { free(rgb); free(line); fclose(f); return false; }
            }
            while (count-- > 0 && pos < need) line[pos++] = (unsigned char)val;
        }
        unsigned char* rPlane = line;
        unsigned char* gPlane = line + bytesPerLine;
        unsigned char* bPlane = line + bytesPerLine * 2;
        for (int x = 0; x < w; x++) {
            unsigned char* p = rgb + (y * w + x) * 3;
            p[0] = rPlane[x]; p[1] = gPlane[x]; p[2] = bPlane[x];
        }
    }

    free(line);
    fclose(f);
    *outRgb = rgb; *outW = w; *outH = h;
    return true;
}

static _Pcx8_* QuantizeRgbAsPcx8(unsigned char* rgb, int w, int h, _Pcx8_* palSrc, const char* resName)
{
    if (!rgb || !palSrc || w <= 0 || h <= 0) return nullptr;
    _Pcx8_* pcx8 = _Pcx8_::CreateNew((char*)resName, w, h);
    if (!pcx8) return nullptr;
    pcx8->SetPaletteFrom(palSrc);

    unsigned char* dst = (unsigned char*)pcx8->buffer;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned char* p = rgb + (y * w + x) * 3;
            int best = 0;
            int bestD = 0x7FFFFFFF;
            for (int i = 1; i < 256; i++) { // 0 通常透明/黑，按钮/背景尽量避开
                int dr = (int)p[0] - (int)palSrc->palette24.colors[i].r;
                int dg = (int)p[1] - (int)palSrc->palette24.colors[i].g;
                int db = (int)p[2] - (int)palSrc->palette24.colors[i].b;
                int d = dr*dr + dg*dg + db*db;
                if (d < bestD) { bestD = d; best = i; if (d == 0) break; }
            }
            dst[y * pcx8->scanline_size + x] = (unsigned char)best;
        }
    }
    pcx8->ref_count = 0x10000;
    return pcx8;
}

static _Pcx8_* LoadPcx24QuantizedAsPcx8(const char* name, _Pcx8_* palSrc)
{
    char path[MAX_PATH];
    GetModuleFileNameA(g_hModule, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) slash[1] = 0; else path[0] = 0;

    char pcxPath[MAX_PATH];
    _snprintf(pcxPath, MAX_PATH, "%spcx\\%s", path, name);

    unsigned char* rgb = nullptr;
    int w = 0, h = 0;
    if (!DecodePcx24ToRgb(pcxPath, &rgb, &w, &h)) return nullptr;

    _Pcx8_* pcx8 = QuantizeRgbAsPcx8(rgb, w, h, palSrc, "bv_q8");
    free(rgb);
    return pcx8;
}

static _Pcx8_* LoadPcx24CompositeAsPcx8(const char* frameName, const char* iconName, _Pcx8_* palSrc)
{
    char path[MAX_PATH];
    GetModuleFileNameA(g_hModule, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) slash[1] = 0; else path[0] = 0;

    char framePath[MAX_PATH], iconPath[MAX_PATH];
    _snprintf(framePath, MAX_PATH, "%spcx\\%s", path, frameName);
    _snprintf(iconPath, MAX_PATH, "%spcx\\%s", path, iconName);

    unsigned char *frame = nullptr, *icon = nullptr;
    int fw = 0, fh = 0, iw = 0, ih = 0;
    if (!DecodePcx24ToRgb(framePath, &frame, &fw, &fh)) return nullptr;
    if (!DecodePcx24ToRgb(iconPath, &icon, &iw, &ih)) { free(frame); return nullptr; }

    unsigned char* comp = (unsigned char*)malloc(fw * fh * 3);
    if (!comp) { free(frame); free(icon); return nullptr; }

    // 先放图标/底图，居中到金框尺寸；外侧用黑色兜底。
    memset(comp, 0, fw * fh * 3);
    int ox = (fw - iw) / 2;
    int oy = (fh - ih) / 2;
    for (int y = 0; y < ih; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= fh) continue;
        for (int x = 0; x < iw; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= fw) continue;
            memcpy(comp + (dy * fw + dx) * 3, icon + (y * iw + x) * 3, 3);
        }
    }

    // 再叠金框；青色(0,255,255)视为透明，不覆盖内部。
    for (int y = 0; y < fh; y++) {
        for (int x = 0; x < fw; x++) {
            unsigned char* fp = frame + (y * fw + x) * 3;
            if (fp[0] == 0 && fp[1] == 255 && fp[2] == 255) continue;
            memcpy(comp + (y * fw + x) * 3, fp, 3);
        }
    }

    _Pcx8_* pcx8 = QuantizeRgbAsPcx8(comp, fw, fh, palSrc, "bv_btn");
    free(frame); free(icon); free(comp);
    return pcx8;
}



// ========== 注册 Hook ==========
