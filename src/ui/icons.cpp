#include "icons.h"
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>

static QPixmap makePixmap(int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    return pm;
}

// ============================================================
// 放大镜（圆 + 斜线）
// ============================================================

QIcon Icons::search(const QColor &color)
{
    QPixmap pm = makePixmap(24);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // 圆: 中心(10,10) 半径6
    p.drawEllipse(QPointF(10, 10), 6, 6);
    // 手柄: 从圆右下到 (19,19)
    p.drawLine(QPointF(14.2, 14.2), QPointF(20, 20));

    return QIcon(pm);
}

// ============================================================
// 齿轮（六齿 + 中心圆）
// ============================================================

QIcon Icons::config(const QColor &color)
{
    QPixmap pm = makePixmap(24);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    double cx = 12.0, cy = 12.0;
    double rInner = 4.5, rOuter = 8.0;
    int teeth = 6;

    QPainterPath path;
    for (int i = 0; i < teeth; i++) {
        double a0 = M_PI * 2.0 * i / teeth;
        double a1 = a0 + M_PI * 2.0 * 0.15 / teeth * 6; // 齿宽角
        double a2 = a0 + M_PI * 2.0 * 0.5 / teeth * 6;
        double a3 = a0 + M_PI * 2.0 * 0.65 / teeth * 6;

        double x0 = cx + rInner * cos(a0);
        double y0 = cy + rInner * sin(a0);
        double x1 = cx + rOuter * cos(a1);
        double y1 = cy + rOuter * sin(a1);
        double x2 = cx + rOuter * cos(a2);
        double y2 = cy + rOuter * sin(a2);
        double x3 = cx + rInner * cos(a3);
        double y3 = cy + rInner * sin(a3);

        if (i == 0)
            path.moveTo(x0, y0);
        else
            path.lineTo(x0, y0);
        path.lineTo(x1, y1);
        path.lineTo(x2, y2);
        path.lineTo(x3, y3);
    }
    path.closeSubpath();
    p.drawPath(path);

    // 中心圆
    p.drawEllipse(QPointF(cx, cy), 2.5, 2.5);

    return QIcon(pm);
}

// ============================================================
// 节点网络（三圆 + 连线）
// ============================================================

QIcon Icons::platform(const QColor &color)
{
    QPixmap pm = makePixmap(24);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // 三个节点
    QPointF top(12, 4);
    QPointF bl(5, 18);
    QPointF br(19, 18);

    // 连线
    p.drawLine(top, bl);
    p.drawLine(top, br);
    p.drawLine(bl, br);

    // 节点圆（fill=白底，覆盖线）
    QBrush bgBrush(QColor(250, 250, 250));
    p.setBrush(bgBrush);
    p.drawEllipse(top, 3, 3);
    p.drawEllipse(bl, 3, 3);
    p.drawEllipse(br, 3, 3);

    return QIcon(pm);
}

// ============================================================
// 数据库（三层椭圆）
// ============================================================

QIcon Icons::memory(const QColor &color)
{
    QPixmap pm = makePixmap(24);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // 顶部椭圆
    p.drawEllipse(QRectF(4, 3, 16, 6));
    // 中间椭圆
    p.drawEllipse(QRectF(4, 9, 16, 6));
    // 底部椭圆
    p.drawEllipse(QRectF(4, 15, 16, 6));

    return QIcon(pm);
}
