// ========== 生物信息窗口第二行：战斗价值 ==========

static int ParseFirstPositiveInt(const char* text)
{
    if (!text) return 0;
    while (*text && (*text < '0' || *text > '9')) ++text;
    int value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        ++text;
    }
    return value;
}

static int GetCreatureFightValueById(int creature_id)
{
    if (creature_id < 0 || creature_id >= 197) return 0;
    char* table = *(char**)0x6747B0;
    if (!table || IsBadReadPtr(table + creature_id * 0x74 + 0x3C, sizeof(int))) return 0;
    return *(int*)(table + creature_id * 0x74 + 0x3C);
}

// 战斗中按队首剩余 HP 折算单只当前价值；无法唯一匹配 stack 时不显示括号值。
static int GetCurrentStackFightValueEstimate(int creature_id, int count, int fight_value)
{
    H3CombatManager* battle_mgr = H3CombatManager::Get();
    if (creature_id < 0 || count <= 0 || fight_value <= 0 || !battle_mgr) return 0;
    if (IsBadReadPtr(battle_mgr, 0x132D0) || !battle_mgr->activeStack) return 0;
    if (IsBadReadPtr(&battle_mgr->stacks[0][0], sizeof(battle_mgr->stacks))) return 0;

    int matches = 0;
    int estimate = 0;
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            const H3CombatCreature& stack = battle_mgr->stacks[side][slot];
            if (stack.type != creature_id || stack.numberAlive != count) continue;
            ++matches;
            int hp = stack.info.hitPoints;
            if (hp <= 0) return 0;
            int lost_hp = stack.healthLost;
            if (lost_hp < 0) lost_hp = 0;
            if (lost_hp >= hp) lost_hp = hp - 1;
            estimate = (fight_value * (hp - lost_hp) + hp / 2) / hp;
            if (matches > 1) return 0;
        }
    }
    return matches == 1 ? estimate : 0;
}

static void AddFightValueLine(_Dlg_* dlg, int fight_value, int current_value)
{
    if (!dlg || fight_value <= 0 || FindDlgItem(dlg, 3008) || FindDlgItem(dlg, 3009)) return;

    char* name_item = FindDlgItem(dlg, 203);
    short y = (short)((name_item ? *(short*)(name_item + 0x1A) : 41) + cfg.fight_value_y_offset);

    H3DlgText* label = H3DlgText::Create(
        25, y, 130, 17, cfg.label_fight_value, (char*)"smalfont.fnt",
        4, 3009, eTextAlignment::TOP_LEFT, 0);
    if (label)
        // BUILD Hook 内只登记新控件，避免默认 initiate=TRUE 重入游戏的 LoadItem。
        reinterpret_cast<H3BaseDlg*>(dlg)->AddItem(reinterpret_cast<H3DlgItem*>(label), FALSE);

    char value_text[64];
    if (current_value > 0)
        _snprintf(value_text, sizeof(value_text) - 1, "%d(%d)", fight_value, current_value);
    else
        _snprintf(value_text, sizeof(value_text) - 1, "%d", fight_value);
    value_text[sizeof(value_text) - 1] = 0;

    H3DlgText* value = H3DlgText::Create(
        148, y, 128, 17, value_text, (char*)"smalfont.fnt",
        4, 3008, eTextAlignment::TOP_RIGHT, 0);
    if (value)
        reinterpret_cast<H3BaseDlg*>(dlg)->AddItem(reinterpret_cast<H3DlgItem*>(value), FALSE);
}

static void TryAddFightValueLine(_Dlg_* dlg)
{
    __try {
        if (!dlg || IsBadReadPtr(dlg, 0x64) || dlg->width != 298 || dlg->height != cfg.window_height
            || !FindDlgItem(dlg, 200) || FindDlgItem(dlg, 3008) || FindDlgItem(dlg, 3009))
            return;

        int creature_id = *(int*)((char*)dlg + 0x60);
        int fight_value = GetCreatureFightValueById(creature_id);
        if (fight_value <= 0) return;

        char* count_item = FindDlgItem(dlg, 204);
        const char* count_text = nullptr;
        if (count_item && !IsBadReadPtr(count_item + 0x38, 1))
            count_text = *(const char**)(count_item + 0x34);
        int count = ParseFirstPositiveInt(count_text);
        int current_value = GetCurrentStackFightValueEstimate(creature_id, count, fight_value);
        AddFightValueLine(dlg, fight_value, current_value);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[FightValue] SEH exception during TryAddFightValueLine dlg=%p", dlg);
    }
}
