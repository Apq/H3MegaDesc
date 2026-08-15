# MegaDesc — 英雄无敌3 大描述框插件

## 简介

MegaDesc 是一个《英雄无敌3》HD Mod 插件，扩展生物信息窗口：加高窗口、替换背景/按钮素材、扩大描述文本区。

## 功能

- 生物信息窗口高度从原版 311 扩展到 383
- 8-bit 索引或 24-bit PCX 背景替换（自带游戏调色板量化）
- 确认/解雇/魔法书按钮图像替换
- 生物名称下方第二行显示“战斗价值”；战斗中可附带队首单只按剩余 HP 折算值
- 描述文本区扩大，支持完整生物描述
- 窗口 Y 坐标自动吸附，防止 HD 高分辨率下越界
- 兼容 H3.TextColor 的长文本颜色标签，修复滚动文本拆行后的跨行颜色继承
- 为右键弹出的通用滚动文本对话框（如魔法详情）提供鼠标滚轮滚动支持；左键弹窗保持原生滚动

### 配置控制

- `[Layout]` 控制窗口尺寸和文本布局
- `[Images]` 自定义背景图
- `[Format] LabelFightValue` 控制战斗价值标签
- `[Logging] DisableLog` 控制日志；`0` 为默认写日志并保留最近 30 个，`1` 为不生成日志且不扫描/清理旧日志

## 依赖

仅依赖原版游戏 EXE + HD Mod 框架（`patcher_x86.hpp`）。不依赖 ZCN2.dll、ERA 或其他插件。

如同时启用 `H3.TextColor.dll`，MegaDesc 会补齐 `_DlgScrollableText_` 长文本拆行后的 `{~Color}` 颜色状态，避免魔法详细描述等滚动文本在换行后丢失自定义文字颜色。未启用 `H3.TextColor.dll` 时不作为硬依赖。

## 编译要求

- Visual Studio（v145 工具集）
- 仅支持 x86（32 位）
- 结构成员对齐：1 字节（/Zp1）
- 源码结构：主文件 `MegaDesc.cpp` + 7 个 `modules/*.inc.cpp`（通过 `#include` 单翻译单元编译）

## 安装

将编译生成的 `MegaDesc.dll` 放入 HD Mod 的 `_HD3_Data\Packs\大描述框\` 目录，在 HD Launcher 中启用插件。
