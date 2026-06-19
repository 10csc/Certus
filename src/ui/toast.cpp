#include "toast.h"
#include "theme.h"
#include <QPainter>
#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QWidget>

QList<Toast*> Toast::s_activeToasts;

Toast::Toast(const QString &msg, Type type, int duration, QWidget *parent)
    : QWidget(parent), m_message(msg), m_type(type)
{
    // 根据类型设置颜色和图标
    switch (type) {
    case Success:
        m_bgColor = QColor(Theme::Success);
        m_icon = "✓";
        break;
    case Warning:
        m_bgColor = QColor(Theme::Warning);
        m_icon = "⚠";
        break;
    case Error:
        m_bgColor = QColor(Theme::Error);
        m_icon = "✕";
        break;
    case Info:
    default:
        m_bgColor = QColor(Theme::Accent);
        m_icon = "ℹ";
        break;
    }

    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);

    // 计算尺寸
    QFont f("Segoe UI", 12);
    QFontMetrics fm(f);
    int textW = fm.horizontalAdvance(m_icon + "  " + m_message);
    int w = textW + 40;
    int h = 40;
    resize(w, h);

    // 透明度效果
    auto *opacity = new QGraphicsOpacityEffect(this);
    opacity->setOpacity(0.0);
    setGraphicsEffect(opacity);

    // 定位：parent 底部居中，堆叠偏移
    if (parent) {
        QRect pr = parent->rect();
        int stackOffset = s_activeToasts.size() * (h + 8);
        int x = pr.width() / 2 - w / 2;
        int y = pr.height() - h - 20 - stackOffset;
        move(parent->mapToGlobal(QPoint(x, y)));
    }

    s_activeToasts.append(this);

    // 淡入动画
    auto *fadeIn = new QPropertyAnimation(opacity, "opacity", this);
    fadeIn->setDuration(200);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(0.95);
    fadeIn->start(QPropertyAnimation::DeleteWhenStopped);

    // 定时消失
    QTimer::singleShot(duration, this, [this, opacity]() {
        auto *fadeOut = new QPropertyAnimation(opacity, "opacity", this);
        fadeOut->setDuration(300);
        fadeOut->setStartValue(0.95);
        fadeOut->setEndValue(0.0);
        connect(fadeOut, &QPropertyAnimation::finished, this, [this]() {
            s_activeToasts.removeOne(this);
            deleteLater();
        });
        fadeOut->start(QPropertyAnimation::DeleteWhenStopped);
    });
}

void Toast::show(const QString &message, Type type, QWidget *parent, int durationMs)
{
    if (!parent) {
        parent = QApplication::activeWindow();
    }
    if (!parent) return;

    // 限制最大同时显示数
    while (s_activeToasts.size() >= 3) {
        s_activeToasts.first()->deleteLater();
        s_activeToasts.removeFirst();
    }

    auto *toast = new Toast(message, type, durationMs, parent);
    toast->QWidget::show();
}

void Toast::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 圆角背景
    p.setBrush(m_bgColor);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), 6, 6);

    // 图标 + 文字
    p.setPen(Qt::white);
    QFont f("Segoe UI", 12);
    p.setFont(f);
    p.drawText(rect(), Qt::AlignCenter, m_icon + "  " + m_message);
}
