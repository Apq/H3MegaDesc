// H3.TextColor already handles rendering of {~Color} tags, but scrollable text is
// pre-split into H3String lines. If a long colored span wraps, later line items
// lose the opening tag. Add the active {~...} tag to wrapped lines after the
// engine/plugin split routine has run.

static bool ReadTextColorTag(const char* text, unsigned int len, unsigned int pos, H3String& outTag)
{
    if (!text || pos + 2 >= len || text[pos] != '{' || text[pos + 1] != '~')
        return false;

    unsigned int end = pos + 2;
    while (end < len && text[end] != '}')
        ++end;

    if (end >= len || end == pos + 2)
        return false;

    outTag.Assign(text + pos, end - pos + 1);
    return true;
}

static void UpdateTextColorState(const H3String& line, H3String& activeTag)
{
    const char* text = line.String();
    unsigned int len = line.Length();
    for (unsigned int i = 0; i < len; ++i) {
        if (text[i] == '{' && i + 1 < len && text[i + 1] == '~') {
            H3String tag;
            if (ReadTextColorTag(text, len, i, tag)) {
                activeTag.Assign(tag);
                i += tag.Length() - 1;
            }
            continue;
        }

        if (text[i] == '}')
            activeTag.Erase();
    }
}

static bool LineStartsWithTextColorTag(const H3String& line)
{
    const char* text = line.String();
    unsigned int len = line.Length();
    unsigned int i = 0;
    while (i < len && (text[i] == ' ' || text[i] == '\t'))
        ++i;

    return i + 1 < len && text[i] == '{' && text[i + 1] == '~';
}

static void PrefixLineWithActiveColor(H3String& line, const H3String& activeTag)
{
    if (activeTag.Empty() || line.Empty() || LineStartsWithTextColorTag(line))
        return;

    H3String patched(activeTag);
    patched.Append(line);
    line.Assign(patched);
}

static void PropagateSplitLineTextColors(H3Vector<H3String>& lines)
{
    H3String activeTag;
    for (unsigned int i = 0; i < lines.Count(); ++i) {
        H3String& line = lines[i];

        if (i > 0)
            PrefixLineWithActiveColor(line, activeTag);

        UpdateTextColorState(line, activeTag);
    }
}

void __stdcall Hook_SplitTextIntoLinesTextColor(HiHook* h, H3Font* font, LPCSTR text, int width, H3Vector<H3String>& lines)
{
    THISCALL_4(void, h->GetDefaultFunc(), font, text, width, &lines);
    PropagateSplitLineTextColors(lines);
}

static void ApplyTextColorScrollableTextFix()
{
    _PI->WriteHiHook(0x4B58F0, SPLICE_, THISCALL_, Hook_SplitTextIntoLinesTextColor);
    WriteLog("TextColor 滚动文本跨行颜色补丁已启用。Hook: H3Font::SplitTextIntoLines(0x4B58F0).");
}
