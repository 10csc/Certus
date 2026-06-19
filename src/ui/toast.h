#pragma once
/**
 * Toast 通知组件 —— 轻量级操作反馈。
 *
 * 在 parent 窗口底部弹出，3 秒自动消失，不阻塞 UI。
 * 4 种类型：Info(蓝) / Success(绿) / Warning(橙) / Error(红)
 */

#include <QWidget>
#include <QColor>
#include <QList>
#include <QTimer>

class Toast : public QWidget {
    Q_OBJECT

public:
    enum Type { Info, Success, Warning, Error };

    /**
     * 在 parent 窗口底部弹出 Toast 通知。
     * @param message    显示文字
     * @param type       通知类型（决定颜色/图标）
     * @param parent     父窗口（为空则使用 activeWindow）
     * @param durationMs 显示时长（毫秒），默认 3000
     */
    static void show(const QString &message, Type type = Info,
                     QWidget *parent = nullptr, int durationMs = 3000);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Toast(const QString &msg, Type type, int duration, QWidget *parent);

    QString m_message;
    Type m_type;
    QColor m_bgColor;
    QString m_icon;

    // 活跃实例管理（用于堆叠定位）
    static QList<Toast*> s_activeToasts;
};
