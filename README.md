# MegaDesc — 英雄无敌3 大描述框插件

## 简介

MegaDesc 是一个《英雄无敌3》HD Mod 插件，扩展生物信息窗口：加高窗口、替换背景/按钮素材、扩大描述文本区。

## 功能

- 生物信息窗口高度从原版 311 扩展到 487
- 24-bit PCX 背景替换（自带调色板量化）
- 确认/解雇/魔法书按钮图像替换
- 描述文本区扩大，支持完整生物描述
- 窗口 Y 坐标自动吸附，防止 HD 高分辨率下越界

### 配置控制

- INI 文件 `[General] Enabled` 为总开关
- `[Layout]` 控制窗口尺寸和文本布局
- `[Images]` 自定义背景图

## 依赖

仅依赖原版游戏 EXE + HD Mod 框架（`patcher_x86.hpp`）。不依赖 ZCN2.dll、ERA 或其他插件。

## 编译要求

- Visual Studio（v145 工具集）
- 仅支持 x86（32 位）
- 结构成员对齐：1 字节（/Zp1）
- 源码结构：主文件 `MegaDesc.cpp` + 4 个 `modules/*.inc.cpp`（通过 `#include` 单翻译单元编译）

## 安装

将编译生成的 `MegaDesc.dll` 放入 HD Mod 的 `_HD3_Data\Packs\大描述框\` 目录，在 HD Launcher 中启用插件。
