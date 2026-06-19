#pragma once
/**
 * Certus 全局主题 —— 集中管理颜色/尺寸常量 + 全局 QSS 样式表。
 *
 * 使用方式：
 *   1. main_gui.cpp 中调用 app.setStyleSheet(globalStyleSheet())
 *   2. 各页面用 setProperty("cssClass", "primary/secondary/danger/success") 区分按钮变体
 *   3. 动态颜色场景引用 Theme:: 常量而非硬编码字符串
 */

#include <QString>

namespace Theme {

// ============================================================
// 调色板
// ============================================================

// 背景层级
constexpr auto BgPrimary   = "#1e1e1e";   // 主背景
constexpr auto BgSecondary = "#252525";   // 次级背景（输入框、表格）
constexpr auto BgTertiary  = "#2c2c2c";   // 三级背景（侧边栏、Tab）
constexpr auto BgInput     = "#333333";   // 输入控件背景
constexpr auto BgHover     = "#3c3c3c";   // hover 背景

// 边框
constexpr auto Border      = "#444444";
constexpr auto BorderLight = "#555555";

// 文字
constexpr auto TextPrimary   = "#cccccc";
constexpr auto TextSecondary = "#aaaaaa";
constexpr auto TextMuted     = "#888888";
constexpr auto TextWhite     = "#ffffff";

// 语义色
constexpr auto Accent      = "#0078d4";   // 主操作蓝
constexpr auto AccentHover = "#0086f0";
constexpr auto Success     = "#4caf50";   // 成功绿
constexpr auto Warning     = "#ff9800";   // 警告橙
constexpr auto Error       = "#f44336";   // 错误红
constexpr auto Info        = "#4fc3f7";   // 信息蓝

// 扩展色
constexpr auto SuccessDark = "#388e3c";
constexpr auto ErrorDark   = "#c62828";
constexpr auto Green       = "#81c784";
constexpr auto Blue        = "#58a6ff";
constexpr auto Purple      = "#d2a8ff";

// ============================================================
// 尺寸
// ============================================================

constexpr int  RadiusSM    = 4;
constexpr int  RadiusMD    = 6;
constexpr int  SpacingSM   = 4;
constexpr int  SpacingMD   = 8;
constexpr int  SpacingLG   = 12;

} // namespace Theme

/**
 * 返回全局 QSS 样式表字符串。
 * 在 QApplication 初始化后调用 app.setStyleSheet(globalStyleSheet())。
 */
QString globalStyleSheet();
