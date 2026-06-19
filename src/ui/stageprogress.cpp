#include "stageprogress.h"
#include "theme.h"
#include <QPainter>

StageProgress::StageProgress(QWidget *parent) : QWidget(parent)
{
    // 动画定时器：驱动 Running 状态的脉冲效果
    m_animTimer.setInterval(400);
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        m_animFrame = (m_animFrame + 1) % 6;
        update();
    });
}

void StageProgress::setStages(const QVector<Stage> &stages)
{
    m_stages = stages;
    m_animFrame = 0;
    bool hasRunning = false;
    for (const auto &s : m_stages) {
        if (s.state == Running) { hasRunning = true; break; }
    }
    if (hasRunning && !m_animTimer.isActive()) m_animTimer.start();
    else if (!hasRunning && m_animTimer.isActive()) m_animTimer.stop();
    update();
}

void StageProgress::updateStage(int index, const Stage &stage)
{
    if (index >= 0 && index < m_stages.size()) {
        m_stages[index] = stage;
        bool hasRunning = false;
        for (const auto &s : m_stages) {
            if (s.state == Running) { hasRunning = true; break; }
        }
        if (hasRunning && !m_animTimer.isActive()) m_animTimer.start();
        else if (!hasRunning && m_animTimer.isActive()) m_animTimer.stop();
        update();
    }
}

void StageProgress::reset()
{
    m_stages.clear();
    m_animTimer.stop();
    m_animFrame = 0;
    update();
}

QSize StageProgress::minimumSizeHint() const
{
    return QSize(200, qMax(36, m_stages.size() * 36));
}

QSize StageProgress::sizeHint() const
{
    return QSize(400, qMax(36, m_stages.size() * 36));
}

void StageProgress::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_stages.isEmpty()) {
        p.setPen(QColor(Theme::TextMuted));
        p.drawText(rect(), Qt::AlignCenter, "等待搜索开始...");
        return;
    }

    int rowH = 36;
    int dotR = 8;
    int leftMargin = 20;
    int dotX = leftMargin + dotR;

    QFont labelFont("Segoe UI", 11);
    QFont statsFont("Segoe UI", 10);

    for (int i = 0; i < m_stages.size(); i++) {
        const auto &s = m_stages[i];
        int y = i * rowH;
        int cy = y + rowH / 2;

        // 绘制连接线（非最后一行时）
        if (i < m_stages.size() - 1) {
            p.setPen(QPen(QColor(Theme::Border), 2));
            p.drawLine(dotX, cy + dotR, dotX, cy + rowH - dotR);
        }

        // 绘制状态圆点
        QColor dotColor;
        QString dotChar;
        switch (s.state) {
        case Pending:
            dotColor = QColor(Theme::TextMuted);
            dotChar = "";
            p.setPen(QPen(dotColor, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPoint(dotX, cy), dotR - 1, dotR - 1);
            break;
        case Running: {
            // 脉冲动画：大小变化
            int pulse = 1 + (m_animFrame % 3);
            dotColor = QColor(Theme::Accent);
            p.setPen(Qt::NoPen);
            p.setBrush(dotColor);
            p.drawEllipse(QPoint(dotX, cy), dotR + pulse - 2, dotR + pulse - 2);
            // 白色中心
            p.setBrush(Qt::white);
            p.drawEllipse(QPoint(dotX, cy), 3, 3);
            break;
        }
        case Done: {
            dotColor = QColor(Theme::Success);
            p.setPen(Qt::NoPen);
            p.setBrush(dotColor);
            p.drawEllipse(QPoint(dotX, cy), dotR, dotR);
            // 白色 ✓
            p.setPen(Qt::white);
            QFont checkFont("Segoe UI", 10, QFont::Bold);
            p.setFont(checkFont);
            p.drawText(QRect(dotX - dotR, cy - dotR, dotR * 2, dotR * 2),
                       Qt::AlignCenter, "✓");
            break;
        }
        case Skipped: {
            dotColor = QColor(Theme::TextMuted);
            p.setPen(Qt::NoPen);
            p.setBrush(dotColor);
            p.drawEllipse(QPoint(dotX, cy), dotR - 2, dotR - 2);
            break;
        }
        case Error: {
            dotColor = QColor(Theme::Error);
            p.setPen(Qt::NoPen);
            p.setBrush(dotColor);
            p.drawEllipse(QPoint(dotX, cy), dotR, dotR);
            p.setPen(Qt::white);
            QFont errFont("Segoe UI", 10, QFont::Bold);
            p.setFont(errFont);
            p.drawText(QRect(dotX - dotR, cy - dotR, dotR * 2, dotR * 2),
                       Qt::AlignCenter, "✕");
            break;
        }
        }

        // 绘制标签
        p.setFont(labelFont);
        QColor labelColor = (s.state == Running) ? QColor(Theme::TextWhite)
                          : (s.state == Done)    ? QColor(Theme::Green)
                          : (s.state == Error)   ? QColor(Theme::Error)
                                                 : QColor(Theme::TextSecondary);
        p.setPen(labelColor);
        int textX = dotX + dotR + 12;
        p.drawText(textX, cy - 2, s.label);

        // 绘制右侧统计信息
        p.setFont(statsFont);
        p.setPen(QColor(Theme::TextMuted));
        if (s.state == Running) {
            // 动画指示器
            static const QStringList spinChars = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴"};
            QString spin = spinChars[m_animFrame % spinChars.size()];
            QString stats = QString("%1 %2s  %3 字符")
                                .arg(spin)
                                .arg(s.elapsedSec)
                                .arg(s.contentLen);
            p.drawText(textX, cy + 14, stats);
        } else if (s.state == Done && s.contentLen > 0) {
            p.drawText(textX, cy + 14,
                       QString("%1 字符  %2s").arg(s.contentLen).arg(s.elapsedSec));
        } else if (s.state == Skipped) {
            p.drawText(textX, cy + 14, "已跳过");
        }
    }
}
