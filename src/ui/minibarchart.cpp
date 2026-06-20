#include "minibarchart.h"
#include "theme.h"
#include <QPainter>
#include <QPaintEvent>
#include <QFont>

MiniBarChart::MiniBarChart(QWidget *parent) : QWidget(parent) {}

void MiniBarChart::setData(const QVector<Bar> &bars, const QString &title)
{
    m_bars = bars;
    m_title = title;
    update();
}

void MiniBarChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 背景
    p.fillRect(rect(), QColor(Theme::BgPrimary));
    p.setPen(QColor(Theme::Border));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    const int leftPad = 80, rightPad = 50, topPad = 30, bottomPad = 10;
    const int chartW = width() - leftPad - rightPad;
    const int chartH = height() - topPad - bottomPad;

    if (m_bars.isEmpty()) {
        p.setPen(QColor(Theme::TextMuted));
        p.setFont(QFont("Segoe UI", 11));
        p.drawText(rect(), Qt::AlignCenter, "暂无数据");
        return;
    }

    // 标题
    if (!m_title.isEmpty()) {
        p.setPen(QColor(Theme::TextPrimary));
        p.setFont(QFont("Segoe UI", 10, QFont::Bold));
        p.drawText(leftPad, 5, chartW, 20, Qt::AlignLeft, m_title);
    }

    // 计算最大值用于缩放
    double maxVal = 0;
    for (const auto &b : m_bars)
        if (b.value > maxVal) maxVal = b.value;
    if (maxVal == 0) maxVal = 1;

    const int barH = qMin(28, (chartH / m_bars.size()) - 6);
    const int gap = barH + 4;
    const int startY = topPad;

    // Y 轴标签 + 网格线
    p.setPen(QColor(Theme::Border));
    p.setFont(QFont("Segoe UI", 8));

    // 柱状图
    for (int i = 0; i < m_bars.size(); i++) {
        const auto &b = m_bars[i];
        int y = startY + i * gap;
        int barW = static_cast<int>(chartW * b.value / maxVal);
        if (barW < 2) barW = 2;

        // 标签（左）
        p.setPen(QColor(Theme::TextSecondary));
        p.setFont(QFont("Segoe UI", 9));
        QRect labelRect(0, y, leftPad - 8, barH);
        p.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, b.label);

        // 柱体
        p.setBrush(b.color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(leftPad, y, barW, barH, 3, 3);

        // 数值标注
        p.setPen(QColor(Theme::TextPrimary));
        p.setFont(QFont("Consolas", 8));
        QString valText = QString::number(b.value, 'f', b.value >= 100 ? 0 : 1);
        p.drawText(leftPad + barW + 6, y, rightPad - 10, barH,
                   Qt::AlignLeft | Qt::AlignVCenter, valText);
    }
}
