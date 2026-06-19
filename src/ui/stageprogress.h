#pragma once
/**
 * StageProgress —— 阶段进度指示器（QPainter 自绘组件）。
 *
 * 可视化显示搜索/整合各阶段的运行状态：
 *   Pending(灰色空心圆) → Running(蓝色脉冲) → Done(绿色✓) / Error(红色✕)
 */

#include <QWidget>
#include <QVector>
#include <QString>
#include <QTimer>

class StageProgress : public QWidget {
    Q_OBJECT

public:
    enum StageState { Pending, Running, Done, Skipped, Error };

    struct Stage {
        QString name;       // 内部标识: "search", "synthesis"
        QString label;      // 显示标签: "搜索 deepseek", "整合 kimi"
        StageState state = Pending;
        int elapsedSec = 0;
        int contentLen = 0;
    };

    explicit StageProgress(QWidget *parent = nullptr);

    void setStages(const QVector<Stage> &stages);
    void updateStage(int index, const Stage &stage);
    void reset();

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<Stage> m_stages;
    QTimer m_animTimer;
    int m_animFrame = 0;
};
