#pragma once
#include <QWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>

class Database;
class MiniBarChart;

class MonitorPage : public QWidget {
    Q_OBJECT
public:
    explicit MonitorPage(QWidget *parent = nullptr);
    void setDatabase(Database *db);
    void refresh();

private slots:
    void onExportData();

private:
    void setupUi();
    void refreshReliability();
    void refreshSourceDistribution();
    void refreshPlatformPerformance();
    void refreshEvolution();
    void refreshFailureAnalysis();

    Database *m_db = nullptr;
    QTabWidget *m_tabs = nullptr;
    QPushButton *m_exportBtn = nullptr;

    // Tab 0: 可靠性总览
    MiniBarChart *m_reliabilityChart = nullptr;
    QLabel *m_reliabilitySummary = nullptr;
    QTableWidget *m_reliabilityTable = nullptr;

    // Tab 1: 信源分布
    QLabel *m_sourceSummary = nullptr;
    QTableWidget *m_sourceTable = nullptr;

    // Tab 2: 平台性能
    QLabel *m_perfSummary = nullptr;
    QTableWidget *m_perfTable = nullptr;

    // Tab 3: 进化状态
    QTableWidget *m_evolutionTable = nullptr;

    // Tab 4: 故障分析
    QLabel *m_failureSummary = nullptr;
    QTableWidget *m_failureTable = nullptr;
    QLabel *m_failureDetailLabel = nullptr;
};
