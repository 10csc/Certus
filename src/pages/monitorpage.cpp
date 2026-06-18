#include "monitorpage.h"
#include "../core/database.h"
#include "../ui/minibarchart.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>

MonitorPage::MonitorPage(QWidget *parent) : QWidget(parent) { setupUi(); }

void MonitorPage::setDatabase(Database *db)
{
    m_db = db;
    refresh();
}

// ============================================================
// UI 布局
// ============================================================

void MonitorPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    // 顶部导出栏
    auto *topRow = new QHBoxLayout();
    auto *title = new QLabel("<b>数据监控</b>", this);
    title->setStyleSheet("color:#ccc; font-size:14px;");
    topRow->addWidget(title);
    topRow->addStretch();
    m_exportBtn = new QPushButton("导出 JSON（AI 分析用）", this);
    m_exportBtn->setStyleSheet(
        "QPushButton{background:#555;color:#ccc;border:none;"
        "border-radius:4px;padding:6px 14px;font-size:12px;}"
        "QPushButton:hover{background:#666;}");
    connect(m_exportBtn, &QPushButton::clicked, this, &MonitorPage::onExportData);
    topRow->addWidget(m_exportBtn);
    layout->addLayout(topRow);

    m_tabs = new QTabWidget(this);
    m_tabs->setStyleSheet(
        "QTabWidget::pane { border:1px solid #444; background:#1e1e1e; }"
        "QTabBar::tab { background:#2c2c2c; color:#aaa; padding:8px 16px; }"
        "QTabBar::tab:selected { background:#333; color:#fff; }");

    auto tableStyle = QString(
        "QTableWidget { background:#1e1e1e; color:#ccc; gridline-color:#444; "
        "border:1px solid #444; }"
        "QHeaderView::section { background:#333; color:#ccc; padding:6px; "
        "border:1px solid #444; }"
        "QTableWidget::item { padding:4px; }");

    // === Tab 0: 可靠性总览 ===
    {
        auto *tab = new QWidget();
        auto *vl = new QVBoxLayout(tab);
        m_reliabilityChart = new MiniBarChart(this);
        m_reliabilitySummary = new QLabel("", this);
        m_reliabilitySummary->setStyleSheet("color:#aaa; padding:4px 0; font-size:12px;");
        vl->addWidget(m_reliabilitySummary);
        vl->addWidget(m_reliabilityChart);
        m_reliabilityTable = new QTableWidget(0, 4, this);
        m_reliabilityTable->setHorizontalHeaderLabels({"平台", "已确认", "推断", "未确认"});
        m_reliabilityTable->setStyleSheet(tableStyle);
        m_reliabilityTable->horizontalHeader()->setStretchLastSection(true);
        vl->addWidget(m_reliabilityTable);
        m_tabs->addTab(tab, "可靠性总览");
    }

    // === Tab 1: 信源分布 ===
    {
        auto *tab = new QWidget();
        auto *vl = new QVBoxLayout(tab);
        m_sourceSummary = new QLabel("", this);
        m_sourceSummary->setStyleSheet("color:#aaa; padding:4px 0; font-size:12px;");
        vl->addWidget(m_sourceSummary);
        m_sourceTable = new QTableWidget(0, 3, this);
        m_sourceTable->setHorizontalHeaderLabels({"类别", "总分", "条目数"});
        m_sourceTable->setStyleSheet(tableStyle);
        m_sourceTable->horizontalHeader()->setStretchLastSection(true);
        vl->addWidget(m_sourceTable);
        m_tabs->addTab(tab, "信源分布");
    }

    // === Tab 2: 平台性能 ===
    {
        auto *tab = new QWidget();
        auto *vl = new QVBoxLayout(tab);
        m_perfSummary = new QLabel("", this);
        m_perfSummary->setStyleSheet("color:#aaa; padding:4px 0; font-size:12px;");
        vl->addWidget(m_perfSummary);
        m_perfTable = new QTableWidget(0, 5, this);
        m_perfTable->setHorizontalHeaderLabels({"平台", "阶段", "平均耗时(s)", "最大耗时(s)", "成功/总数"});
        m_perfTable->setStyleSheet(tableStyle);
        m_perfTable->horizontalHeader()->setStretchLastSection(true);
        vl->addWidget(m_perfTable);
        m_tabs->addTab(tab, "平台性能");
    }

    // === Tab 3: 进化状态 ===
    {
        m_evolutionTable = new QTableWidget(0, 4, this);
        m_evolutionTable->setHorizontalHeaderLabels({"平台", "配置项", "旧值", "新值"});
        m_evolutionTable->setStyleSheet(tableStyle);
        m_evolutionTable->horizontalHeader()->setStretchLastSection(true);
        m_tabs->addTab(m_evolutionTable, "进化状态");
    }

    // === Tab 4: 故障分析 ===
    {
        auto *tab = new QWidget();
        auto *vl = new QVBoxLayout(tab);
        m_failureSummary = new QLabel("", this);
        m_failureSummary->setStyleSheet("color:#aaa; padding:4px 0; font-size:12px;");
        vl->addWidget(m_failureSummary);
        m_failureTable = new QTableWidget(0, 2, this);
        m_failureTable->setHorizontalHeaderLabels({"错误类型", "次数"});
        m_failureTable->setStyleSheet(tableStyle);
        m_failureTable->horizontalHeader()->setStretchLastSection(true);
        vl->addWidget(m_failureTable);
        m_failureDetailLabel = new QLabel("选择错误类型查看详情", this);
        m_failureDetailLabel->setStyleSheet("color:#888; padding:8px;");
        m_failureDetailLabel->setWordWrap(true);
        vl->addWidget(m_failureDetailLabel);
        m_tabs->addTab(tab, "故障分析");
    }

    layout->addWidget(m_tabs);
}

// ============================================================
// 刷新逻辑
// ============================================================

void MonitorPage::refresh()
{
    if (!m_db) return;
    refreshReliability();
    refreshSourceDistribution();
    refreshPlatformPerformance();
    refreshEvolution();
    refreshFailureAnalysis();
}

void MonitorPage::refreshReliability()
{
    auto agg = m_db->aggregateReliabilityByPlatform(50);

    // 汇总分析
    int totalC = 0, totalI = 0, totalU = 0;
    for (const auto &a : agg) {
        totalC += a.totalConfirmed; totalI += a.totalInferred; totalU += a.totalUnconfirmed;
    }
    int totalAll = totalC + totalI + totalU;
    if (totalAll > 0) {
        double cRate = 100.0 * totalC / totalAll;
        m_reliabilitySummary->setStyleSheet(
            cRate >= 70 ? "color:#4caf50; padding:4px 0; font-size:12px;" :
            cRate >= 40 ? "color:#ff9800; padding:4px 0; font-size:12px;" :
                          "color:#f44336; padding:4px 0; font-size:12px;");
        m_reliabilitySummary->setText(
            QString("分析: 共 %1 条可靠性记录，已确认率 %2%。%3个平台参与统计。")
                .arg(totalAll).arg(cRate, 0, 'f', 1).arg(agg.size()));
    } else {
        m_reliabilitySummary->setText("暂无数据。完成搜索后将在此显示可靠性分析。");
    }

    QVector<MiniBarChart::Bar> bars;
    const QColor colors[] = {
        QColor(0, 120, 212), QColor(76, 175, 80),
        QColor(255, 152, 0), QColor(244, 67, 54),
    };

    m_reliabilityTable->setRowCount(agg.size());
    for (int i = 0; i < agg.size(); i++) {
        const auto &a = agg[i];
        m_reliabilityTable->setItem(i, 0, new QTableWidgetItem(a.platform));
        m_reliabilityTable->setItem(i, 1, new QTableWidgetItem(QString::number(a.totalConfirmed)));
        m_reliabilityTable->setItem(i, 2, new QTableWidgetItem(QString::number(a.totalInferred)));
        m_reliabilityTable->setItem(i, 3, new QTableWidgetItem(QString::number(a.totalUnconfirmed)));

        MiniBarChart::Bar bar;
        bar.label = a.platform;
        bar.value = a.totalConfirmed + a.totalInferred + a.totalUnconfirmed;
        bar.color = colors[i % 4];
        bars.append(bar);
    }
    m_reliabilityChart->setData(bars, "各平台可靠性点数分布");
}

void MonitorPage::refreshSourceDistribution()
{
    auto agg = m_db->sourceScoreByCategory();

    if (agg.isEmpty()) {
        m_sourceSummary->setText("暂无数据。");
    } else {
        int totalEntries = 0, totalScore = 0;
        for (const auto &a : agg) { totalEntries += a.entryCount; totalScore += a.totalScore; }
        double avg = totalEntries > 0 ? 1.0 * totalScore / totalEntries : 0;
        m_sourceSummary->setStyleSheet(
            avg >= 7 ? "color:#4caf50; padding:4px 0; font-size:12px;" :
            avg >= 5 ? "color:#ff9800; padding:4px 0; font-size:12px;" :
                       "color:#f44336; padding:4px 0; font-size:12px;");
        m_sourceSummary->setText(
            QString("分析: %1 个类别共 %2 个信源，平均可信度 %3/10。%4")
                .arg(agg.size()).arg(totalEntries)
                .arg(avg, 0, 'f', 1)
                .arg(avg >= 7 ? "整体可靠" : avg >= 5 ? "存在风险" : "建议交叉验证"));
    }

    m_sourceTable->setRowCount(agg.size());
    for (int i = 0; i < agg.size(); i++) {
        const auto &a = agg[i];
        m_sourceTable->setItem(i, 0, new QTableWidgetItem(a.category));
        m_sourceTable->setItem(i, 1, new QTableWidgetItem(QString::number(a.totalScore)));
        m_sourceTable->setItem(i, 2, new QTableWidgetItem(QString::number(a.entryCount)));
    }
}

void MonitorPage::refreshPlatformPerformance()
{
    auto agg = m_db->platformPerformance();

    if (agg.isEmpty()) {
        m_perfSummary->setText("暂无数据。");
    } else {
        // 找最快和最慢的平台
        double bestAvg = 999, worstAvg = 0;
        QString bestPlat, worstPlat;
        int totalRuns = 0, totalSuccess = 0;
        for (const auto &a : agg) {
            totalRuns += a.runCount; totalSuccess += a.successCount;
            if (a.avgElapsed < bestAvg) { bestAvg = a.avgElapsed; bestPlat = a.platform; }
            if (a.avgElapsed > worstAvg && a.runCount > 0) { worstAvg = a.avgElapsed; worstPlat = a.platform; }
        }
        double successRate = totalRuns > 0 ? 100.0 * totalSuccess / totalRuns : 0;
        m_perfSummary->setStyleSheet(
            successRate >= 90 ? "color:#4caf50; padding:4px 0; font-size:12px;" :
            successRate >= 70 ? "color:#ff9800; padding:4px 0; font-size:12px;" :
                                "color:#f44336; padding:4px 0; font-size:12px;");
        m_perfSummary->setText(
            QString("分析: %1 次调用，成功率 %2%。最快: %3 (均%4s)，最慢: %5 (均%6s)")
                .arg(totalRuns).arg(successRate, 0, 'f', 1)
                .arg(bestPlat).arg(bestAvg, 0, 'f', 1)
                .arg(worstPlat).arg(worstAvg, 0, 'f', 1));
    }

    m_perfTable->setRowCount(agg.size());
    for (int i = 0; i < agg.size(); i++) {
        const auto &a = agg[i];
        m_perfTable->setItem(i, 0, new QTableWidgetItem(a.platform));
        m_perfTable->setItem(i, 1, new QTableWidgetItem(a.stage));
        m_perfTable->setItem(i, 2, new QTableWidgetItem(
            QString::number(a.avgElapsed, 'f', 1)));
        m_perfTable->setItem(i, 3, new QTableWidgetItem(
            QString::number(a.maxElapsed, 'f', 1)));
        m_perfTable->setItem(i, 4, new QTableWidgetItem(
            QString("%1/%2").arg(a.successCount).arg(a.runCount)));
    }
}

void MonitorPage::refreshEvolution()
{
    auto changes = m_db->recentEvolutionChanges(100);
    m_evolutionTable->setRowCount(changes.size());
    for (int i = 0; i < changes.size(); i++) {
        const auto &c = changes[i];
        m_evolutionTable->setItem(i, 0, new QTableWidgetItem(c.platform));
        m_evolutionTable->setItem(i, 1, new QTableWidgetItem(c.key));
        m_evolutionTable->setItem(i, 2, new QTableWidgetItem(c.oldValue));
        m_evolutionTable->setItem(i, 3, new QTableWidgetItem(c.newValue));
    }
}

void MonitorPage::refreshFailureAnalysis()
{
    auto agg = m_db->failureByType(30);

    if (agg.isEmpty()) {
        m_failureSummary->setText("近 30 天无故障记录，系统运行正常 ✓");
        m_failureSummary->setStyleSheet("color:#4caf50; padding:4px 0; font-size:12px;");
    } else {
        int totalF = 0;
        for (const auto &a : agg) totalF += a.count;
        QString topType = agg.first().errorType;
        m_failureSummary->setStyleSheet("color:#f44336; padding:4px 0; font-size:12px;");
        m_failureSummary->setText(
            QString("分析: 近 30 天共 %1 次故障，最多为「%2」。建议检查对应平台状态和网络连接。")
                .arg(totalF).arg(topType));
    }

    m_failureTable->setRowCount(agg.size());
    for (int i = 0; i < agg.size(); i++) {
        const auto &a = agg[i];
        m_failureTable->setItem(i, 0, new QTableWidgetItem(a.errorType));
        m_failureTable->setItem(i, 1, new QTableWidgetItem(QString::number(a.count)));
    }

    if (agg.isEmpty()) {
        m_failureDetailLabel->setText("暂无故障记录");
    }
}

// ============================================================
// 数据导出
// ============================================================

void MonitorPage::onExportData()
{
    if (!m_db) {
        QMessageBox::warning(this, "导出失败", "数据库未连接");
        return;
    }

    QString defaultName = QString("certus_export_%1.json")
                              .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString path = QFileDialog::getSaveFileName(
        this, "导出数据为 JSON", defaultName,
        "JSON 文件 (*.json);;所有文件 (*)");

    if (path.isEmpty()) return;

    // 导出全部表（无 project 过滤——监控页看全量）
    QJsonObject data = m_db->exportAllData();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "导出失败", "无法写入文件: " + path);
        return;
    }
    QJsonDocument doc(data);
    file.write(doc.toJson());
    file.close();

    int histCount = data["search_history"].toArray().size();
    int relCount = data["reliability"].toArray().size();
    int srcCount = data["source_scores"].toArray().size();
    int perfCount = data["platform_performance"].toArray().size();
    QMessageBox::information(this, "导出完成",
        QString("数据已保存到:\n%1\n\n%2 条搜索, %3 条可靠性, %4 条信源, %5 条性能")
            .arg(path).arg(histCount).arg(relCount).arg(srcCount).arg(perfCount));
}
