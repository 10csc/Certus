#pragma once
/**
 * MiniBarChart —— 暗色主题水平柱状图（QPainter 实现，零额外依赖）。
 *
 * 用于 MonitorPage 的数据可视化，替代 QtCharts 模块。
 */
#include <QWidget>
#include <QVector>
#include <QString>
#include <QColor>

class MiniBarChart : public QWidget {
    Q_OBJECT
public:
    struct Bar {
        QString label;
        double value;
        QColor color;
    };

    explicit MiniBarChart(QWidget *parent = nullptr);
    void setData(const QVector<Bar> &bars, const QString &title = {});

    QSize minimumSizeHint() const override { return QSize(200, 140); }
    QSize sizeHint() const override { return QSize(400, 200); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<Bar> m_bars;
    QString m_title;
};
