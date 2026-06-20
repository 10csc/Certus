#pragma once
/**
 * Certus SVG 线条图标工具类 —— QPainter 手绘，无 QtSvg 依赖。
 *
 * 每个函数返回 QIcon，包含 normal/disabled 两种状态的 pixmap。
 * 传入 QColor 控制颜色（默认深色，选中时蓝色）。
 */

#include <QIcon>
#include <QColor>

namespace Icons {

QIcon search(const QColor &color = QColor(26, 26, 26));
QIcon config(const QColor &color = QColor(26, 26, 26));
QIcon platform(const QColor &color = QColor(26, 26, 26));
QIcon memory(const QColor &color = QColor(26, 26, 26));

} // namespace Icons
