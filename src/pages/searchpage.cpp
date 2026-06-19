#include "searchpage.h"
#include "../core/agentmanager.h"
#include "../core/database.h"
#include "../core/browsermanager.h"
#include "../ui/theme.h"
#include "../ui/stageprogress.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFile>
#include <QTextStream>
#include <QScrollBar>
#include <QTextCursor>
#include <QDateTime>
#include <QRegularExpression>
#include <QMessageBox>

// 前向声明
static QString markdownToHtml(QString md);
static QString inlineMarkdown(const QString &text);
static void finishTable(QString &html, const QStringList &lines);

// ============================================================
// 构造
// ============================================================

SearchPage::SearchPage(QWidget *parent) : QWidget(parent)
{
    setupUi();

    // 每 5 秒检查一次配置完整性
    m_configTimer = new QTimer(this);
    connect(m_configTimer, &QTimer::timeout, this, &SearchPage::checkConfigStatus);
    m_configTimer->start(5000);
}

void SearchPage::setAgentManager(AgentManager *agent)
{
    m_agent = agent;
    if (agent) {
        connect(agent, &AgentManager::stageStarted,
                this, &SearchPage::onStageStarted);
        connect(agent, &AgentManager::stageFinished,
                this, &SearchPage::onStageFinished);
        connect(agent, &AgentManager::stageProgress,
                this, &SearchPage::onStageProgress);
        connect(agent, &AgentManager::searchFinished,
                this, &SearchPage::onSearchFinished);
        connect(agent, &AgentManager::errorOccurred,
                this, &SearchPage::onErrorOccurred);
        // 缓存系统信号
        connect(agent, &AgentManager::cacheHit,
                this, &SearchPage::onCacheHit);
        connect(agent, &AgentManager::cacheMiss,
                this, &SearchPage::onCacheMiss);
    }
}

void SearchPage::setDatabase(Database *db)
{
    m_db = db;
    refreshHistory();

    // 从 SQLite 读取持久化的平台默认值，更新只读标签
    if (db) {
        QString sp = db->loadConfig("search_platform", "deepseek");
        m_searchPlatformLabel->setText(QString("搜索: %1").arg(sp));
        QString kp = db->loadConfig("synthesis_platform", "kimi");
        m_synthesisPlatformLabel->setText(QString("整合: %1").arg(kp));
    }
}

void SearchPage::setBrowserManager(BrowserManager *browser)
{
    m_browser = browser;
    updateBrowserStatus();
}

// ============================================================
// UI 布局
// ============================================================

void SearchPage::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(8);

    // === 品牌标题（空闲时显示） ===
    m_brandTitle = new QLabel("Certus — AI 研究助手", this);
    m_brandTitle->setAlignment(Qt::AlignCenter);
    m_brandTitle->setStyleSheet(
        QString("color: %1; font-size: 20px; font-weight: 300; padding: 8px 0;").arg(Theme::TextMuted));
    mainLayout->addWidget(m_brandTitle);

    // === 搜索框区（居中、突出） ===
    auto *inputRow = new QHBoxLayout();
    m_queryInput = new QLineEdit(this);
    m_queryInput->setPlaceholderText("输入你想研究的问题...");
    m_queryInput->setMinimumHeight(48);
    m_queryInput->setStyleSheet(
        QString("QLineEdit { background: %1; color: %2; border: 2px solid %3; "
                "border-radius: 8px; padding: 12px 16px; font-size: 16px; }"
                "QLineEdit:focus { border: 2px solid %4; }")
            .arg(Theme::BgInput, Theme::TextWhite, Theme::BorderLight, Theme::Accent));

    m_searchButton = new QPushButton("搜索", this);
    m_searchButton->setMinimumHeight(48);
    m_searchButton->setMinimumWidth(110);
    m_searchButton->setProperty("cssClass", "primary");
    m_searchButton->setStyleSheet(
        QString("QPushButton { background: %1; color: white; border: none; "
                "border-radius: 8px; font-size: 15px; font-weight: bold; padding: 12px 24px; }"
                "QPushButton:hover { background: %2; }"
                "QPushButton:disabled { background: %3; color: %4; }")
            .arg(Theme::Accent, Theme::AccentHover, Theme::Border, Theme::TextMuted));
    connect(m_searchButton, &QPushButton::clicked, this, &SearchPage::onStartSearch);
    inputRow->addWidget(m_queryInput, 1);
    inputRow->addWidget(m_searchButton);
    mainLayout->addLayout(inputRow);

    // === 选项行（紧凑、无 GroupBox） ===
    auto *optionsRow = new QHBoxLayout();
    optionsRow->setSpacing(12);
    auto *depthLabel = new QLabel("深度:", this);
    depthLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::TextSecondary));
    m_l2Radio = new QRadioButton("L2", this);
    m_l3Radio = new QRadioButton("L3", this);
    m_l2Radio->setChecked(true);
    optionsRow->addWidget(depthLabel);
    optionsRow->addWidget(m_l2Radio);
    optionsRow->addWidget(m_l3Radio);
    optionsRow->addSpacing(8);

    // 平台只读标签
    m_searchPlatformLabel = new QLabel("搜索: deepseek", this);
    m_searchPlatformLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::Info));
    optionsRow->addWidget(m_searchPlatformLabel);
    m_synthesisPlatformLabel = new QLabel("整合: kimi", this);
    m_synthesisPlatformLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::Green));
    optionsRow->addWidget(m_synthesisPlatformLabel);
    optionsRow->addSpacing(8);

    // 浏览器状态
    m_browserStatus = new QLabel("浏览器: 未检测", this);
    m_browserStatus->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::TextSecondary));
    optionsRow->addWidget(m_browserStatus);
    optionsRow->addSpacing(8);

    // 配置状态
    m_configStatus = new QLabel("配置: 检测中...", this);
    m_configStatus->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::TextMuted));
    optionsRow->addWidget(m_configStatus);

    // 状态指示器
    m_statusIndicator = new QLabel("○", this);
    m_statusIndicator->setStyleSheet(QString("color: %1; font-size: 18px;").arg(Theme::TextMuted));
    m_statusIndicator->setToolTip("空闲");
    optionsRow->addWidget(m_statusIndicator);

    m_statusAnimTimer = new QTimer(this);
    m_statusAnimTimer->setInterval(400);
    connect(m_statusAnimTimer, &QTimer::timeout, this, &SearchPage::updateStatusAnimation);

    optionsRow->addStretch();
    mainLayout->addLayout(optionsRow);

    // === 分隔线 ===
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QString("color: %1;").arg(Theme::Border));
    sep->setFixedHeight(1);
    mainLayout->addWidget(sep);

    // === 阶段进度指示器 ===
    m_stageProgress = new StageProgress(this);
    m_stageProgress->setMaximumHeight(80);
    mainLayout->addWidget(m_stageProgress);

    // === 详细日志（可折叠，默认隐藏） ===
    m_toggleLogBtn = new QPushButton("▶ 详细日志", this);
    m_toggleLogBtn->setStyleSheet(
        QString("QPushButton { background: transparent; color: %1; border: none; "
                "text-align: left; padding: 4px 0; font-size: 12px; }"
                "QPushButton:hover { color: %2; }")
            .arg(Theme::TextMuted, Theme::TextPrimary));
    connect(m_toggleLogBtn, &QPushButton::clicked, this, [this]() {
        bool visible = !m_progressLog->isVisible();
        m_progressLog->setVisible(visible);
        m_toggleLogBtn->setText(visible ? "▼ 详细日志" : "▶ 详细日志");
    });
    mainLayout->addWidget(m_toggleLogBtn);

    m_progressLog = new QTextEdit(this);
    m_progressLog->setReadOnly(true);
    m_progressLog->setMaximumHeight(120);
    m_progressLog->setPlaceholderText("搜索进度将在此显示...");
    m_progressLog->setVisible(false);  // 默认隐藏
    mainLayout->addWidget(m_progressLog);

    // === 报告视图（占满剩余空间） ===
    m_reportView = new QTextBrowser(this);
    m_reportView->setOpenExternalLinks(true);
    m_reportView->setStyleSheet(
        QString("QTextBrowser { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 6px; font-size: 13px; }")
            .arg("#1a1a1a", "#ddd", Theme::Border));
    m_reportView->setHtml(
        "<div style='color:#888; text-align:center; margin-top:80px;'>"
        "<p style='font-size:18px;'>Certus 研究报告</p>"
        "<p>输入问题并点击「搜索」</p></div>");
    mainLayout->addWidget(m_reportView, 1);

    // === 历史记录（可折叠，默认收起） ===
    auto *historySep = new QFrame(this);
    historySep->setFrameShape(QFrame::HLine);
    historySep->setStyleSheet(QString("color: %1;").arg(Theme::Border));
    historySep->setFixedHeight(1);
    mainLayout->addWidget(historySep);

    m_historyToggle = new QLabel("最近搜索 ▸", this);
    m_historyToggle->setStyleSheet(
        QString("QLabel { color: %1; font-size: 13px; padding: 4px 0; }"
                "QLabel:hover { color: %2; }")
            .arg(Theme::TextSecondary, Theme::TextPrimary));
    m_historyToggle->setCursor(Qt::PointingHandCursor);
    connect(m_historyToggle, &QLabel::linkActivated, this, [](const QString &){});
    // 用 mousePressEvent 通过 eventFilter 太复杂，改用 QPushButton 模拟
    auto *toggleBtn = new QPushButton("最近搜索 ▸", this);
    toggleBtn->setFlat(true);
    toggleBtn->setStyleSheet(
        QString("QPushButton { background: transparent; color: %1; border: none; "
                "text-align: left; padding: 6px 0; font-size: 13px; }"
                "QPushButton:hover { color: %2; }")
            .arg(Theme::TextSecondary, Theme::TextPrimary));
    // 删除 m_historyToggle QLabel，改用按钮
    delete m_historyToggle;
    m_historyToggle = nullptr;
    connect(toggleBtn, &QPushButton::clicked, this, [this, toggleBtn]() {
        m_historyExpanded = !m_historyExpanded;
        m_historyFilter->setVisible(m_historyExpanded);
        m_historyList->setVisible(m_historyExpanded);
        toggleBtn->setText(m_historyExpanded ? "最近搜索 ▾" : "最近搜索 ▸");
    });
    mainLayout->addWidget(toggleBtn);

    m_historyFilter = new QLineEdit(this);
    m_historyFilter->setPlaceholderText("搜索历史记录...");
    m_historyFilter->setVisible(false);
    connect(m_historyFilter, &QLineEdit::textChanged, this, &SearchPage::refreshHistory);
    mainLayout->addWidget(m_historyFilter);

    m_historyList = new QListWidget(this);
    m_historyList->setMaximumHeight(120);
    m_historyList->setVisible(false);
    connect(m_historyList, &QListWidget::itemClicked,
            this, &SearchPage::onHistoryItemClicked);
    mainLayout->addWidget(m_historyList);
}

// ============================================================
// 搜索操作
// ============================================================

void SearchPage::updateBrowserStatus()
{
    if (!m_browser) {
        m_browserStatus->setText("浏览器: 未初始化");
        return;
    }
    // 从配置读取端口，优先扫描用户设定的端口
    int configPort = m_db ? m_db->loadConfig("cdp_port", "9223").toInt() : 9223;
    int port = m_browser->scanCdpPort(configPort, configPort + 3);
    if (port > 0) {
        m_browserStatus->setStyleSheet(
            QString("color: %1; font-size: 12px;").arg(Theme::Success));
        m_browserStatus->setText(
            QString("浏览器: 已连接 (端口 %1)").arg(port));
    } else {
        m_browserStatus->setStyleSheet(
            QString("color: %1; font-size: 12px;").arg(Theme::Warning));
        m_browserStatus->setText("浏览器: 未检测到 CDP");
    }
}

void SearchPage::checkConfigStatus()
{
    if (!m_agent || !m_db) return;

    // 每 5s 从 SQLite 同步最新的平台和端口配置（解决 ConfigPage 修改后不同步问题）
    QString sp = m_db->loadConfig("search_platform", "deepseek");
    m_searchPlatformLabel->setText(QString("搜索: %1").arg(sp));
    QString kp = m_db->loadConfig("synthesis_platform", "kimi");
    m_synthesisPlatformLabel->setText(QString("整合: %1").arg(kp));

    // 自动深度分析：启用时灰掉手动选择
    bool autoDepth = m_db->loadConfig("auto_depth", "1") == "1";
    m_l2Radio->setEnabled(!autoDepth);
    m_l3Radio->setEnabled(!autoDepth);

    // 同步浏览器状态（一次扫描，两处使用）
    int configPort = m_db->loadConfig("cdp_port", "9223").toInt();
    bool browserReady = false;
    int actualPort = 0;
    if (m_browser) {
        actualPort = m_browser->scanCdpPort(configPort, configPort + 3);
        browserReady = (actualPort > 0);
    }
    if (browserReady) {
        m_browserStatus->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::Success));
        m_browserStatus->setText(QString("浏览器: 端口 %1").arg(actualPort));
    } else {
        m_browserStatus->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::Warning));
        m_browserStatus->setText(QString("浏览器: 未检测 (配置%d)").arg(configPort));
    }

    // 配置完整性检查（含浏览器状态）
    auto issues = m_agent->validateConfig();
    bool hasBlocking = false;
    bool hasWarning = false;
    QStringList messages;

    for (const auto &iss : issues) {
        if (iss.blocking) {
            hasBlocking = true;
            messages.append(QString("✗ %1").arg(iss.message));
        } else {
            hasWarning = true;
            messages.append(QString("△ %1").arg(iss.message));
        }
    }

    // CDP 未检测到 → 合并到配置状态
    if (!browserReady) {
        hasWarning = true;
        messages.append("△ 浏览器未启动 (请在配置页启动)");
    }

    if (messages.isEmpty()) {
        m_configStatus->setStyleSheet(QString("color: %1; font-size: 11px; padding: 2px 0;").arg(Theme::Success));
        m_configStatus->setText("配置: ✓ 就绪");
    } else if (hasBlocking) {
        m_configStatus->setStyleSheet(QString("color: %1; font-size: 11px; padding: 2px 0;").arg(Theme::Error));
        m_configStatus->setText("配置: " + messages.join("  "));
    } else {
        m_configStatus->setStyleSheet(QString("color: %1; font-size: 11px; padding: 2px 0;").arg(Theme::Warning));
        m_configStatus->setText("配置: " + messages.join("  "));
    }
}

void SearchPage::logWarning(const QString &msg)
{
    m_progressLog->append(
        QString("<span style='color:%1;'>⚠ %2</span>").arg(Theme::Warning, msg));
    m_progressLog->verticalScrollBar()->setValue(
        m_progressLog->verticalScrollBar()->maximum());
}

void SearchPage::logError(const QString &msg)
{
    m_progressLog->append(
        QString("<span style='color:%1;'>✗ %2</span>").arg(Theme::Error, msg));
    m_progressLog->verticalScrollBar()->setValue(
        m_progressLog->verticalScrollBar()->maximum());
}

void SearchPage::onStartSearch()
{
    QString query = m_queryInput->text().trimmed();
    if (query.isEmpty() || !m_agent) return;

    // === 缓存命中检查：如果上次搜索后有缓存结果，且查询相似，弹窗提示 ===
    if (m_cacheQueried && !m_cachedMatches.isEmpty()
        && !m_lastSearchedQuery.isEmpty()) {
        // 简单相似度检查：查询字符串重叠度 >= 60% 则认为相似
        auto best = m_cachedMatches.first().toObject();
        QString prevQuery = best["query"].toString();
        int overlap = 0;
        for (int i = 0; i < qMin(prevQuery.size(), query.size()); ++i) {
            if (prevQuery[i] == query[i]) overlap++;
        }
        double charSim = static_cast<double>(overlap)
                         / qMax(prevQuery.size(), query.size());

        if (charSim >= 0.6) {
            double sim = best["similarity"].toDouble();
            QString reportPath = best["report_path"].toString();
            auto btn = QMessageBox::question(
                this, "缓存命中",
                QString("发现相似历史报告（相似度 %1%）：\n\n"
                        "「%2」\n\n"
                        "是否直接查看历史报告？\n"
                        "点击“否”继续新搜索，点击“取消”返回。")
                    .arg(static_cast<int>(sim * 100))
                    .arg(prevQuery.left(80)),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                QMessageBox::No);
            if (btn == QMessageBox::Yes && QFile::exists(reportPath)) {
                loadReport(reportPath);
                return;
            } else if (btn == QMessageBox::Cancel) {
                return;
            }
            // No = 继续新搜索
        }
        // 重置缓存状态
        m_cacheQueried = false;
        m_cachedMatches = QJsonArray();
    }

    // === 搜索前配置预检 ===
    auto issues = m_agent->validateConfig();
    QStringList blockingReasons;
    for (const auto &iss : issues) {
        if (iss.blocking) {
            blockingReasons.append(iss.message);
        }
    }
    if (!blockingReasons.isEmpty()) {
        logError(QString("配置不完整，无法开始搜索: %1。请前往「配置」页修复后重试")
                     .arg(blockingReasons.join("; ")));
        return;
    }
    // 非阻断警告记录到日志
    for (const auto &iss : issues) {
        if (!iss.blocking) {
            logWarning(iss.message);
        }
    }

    // 检查浏览器可用性——不可用时自动热启动
    int configPort = m_db ? m_db->loadConfig("cdp_port", "9223").toInt() : 9223;
    QString cdpPort = QString::number(configPort);
    if (m_browser) {
        int port = m_browser->scanCdpPort(configPort, configPort + 3);
        if (port > 0) {
            cdpPort = QString::number(port);
        } else {
            // CDP 不可用 → 尝试自动启动浏览器
            m_progressLog->append(
                QString("<span style='color:%1;'>浏览器未就绪，正在自动启动...</span>").arg(Theme::Warning));
            int launchedPort = m_browser->launch(configPort);
            if (launchedPort > 0) {
                cdpPort = QString::number(launchedPort);
                m_progressLog->append(
                    QString("<span style='color:%1;'>✓ 浏览器已启动 (端口 %2)</span>")
                        .arg(Theme::Green).arg(launchedPort));
                // 立即刷新状态标签，不等 5s 定时器
                checkConfigStatus();
            } else {
                logError(QString(
                    "浏览器启动失败（端口 %1）。请关闭所有 Edge/Chrome 窗口后重试，"
                    "或前往「配置」页手动启动。").arg(configPort));
                return;
            }
        }
    }

    QString depth = m_l3Radio->isChecked() ? "L3" : "L2";
    // 从只读标签提取平台名（格式: "搜索: deepseek" → "deepseek"）
    QString sp = m_searchPlatformLabel->text().section(": ", 1, 1);
    QString kp = m_synthesisPlatformLabel->text().section(": ", 1, 1);

    setSearchEnabled(false);
    m_progressLog->clear();
    m_progressLog->append(
        QString("[%1] 开始搜索: %2").arg(
            QDateTime::currentDateTime().toString("hh:mm:ss"), query));

    // 初始化 StageProgress
    {
        QString sp = m_searchPlatformLabel->text().section(": ", 1, 1);
        QString kp = m_synthesisPlatformLabel->text().section(": ", 1, 1);
        QVector<StageProgress::Stage> stages;
        stages.append({"search", QString("搜索 %1").arg(sp), StageProgress::Pending, 0, 0});
        stages.append({"synthesis", QString("整合 %1").arg(kp), StageProgress::Pending, 0, 0});
        m_stageProgress->setStages(stages);
    }

    // 状态灯：蓝色搜索中
    m_statusIndicator->setText("●");
    m_statusIndicator->setStyleSheet(QString("color: %1; font-size: 18px;").arg(Theme::Accent));
    m_statusIndicator->setToolTip("搜索中...");
    m_statusAnimTimer->start();

    m_agent->start(query, depth, sp, kp, cdpPort, false);
}

void SearchPage::setSearchEnabled(bool enabled)
{
    m_searchButton->setEnabled(enabled);
    m_queryInput->setEnabled(enabled);
    m_l2Radio->setEnabled(enabled);
    m_l3Radio->setEnabled(enabled);
}

// ============================================================
// 事件响应
// ============================================================

void SearchPage::onStageStarted(const QString &stage, const QString &question,
                                const QString &platform)
{
    m_progressLog->append(
        QString("[%1] %2 | %3").arg(stage, question.left(60), platform));
    m_progressLog->verticalScrollBar()->setValue(
        m_progressLog->verticalScrollBar()->maximum());

    // 更新 StageProgress
    int idx = (stage == "synthesis") ? 1 : 0;
    StageProgress::Stage s;
    s.name = stage;
    s.label = QString("%1 %2").arg(stage == "synthesis" ? "整合" : "搜索", platform);
    s.state = StageProgress::Running;
    m_stageProgress->updateStage(idx, s);
}

void SearchPage::onStageProgress(const QString &stage, const QString &platform,
                                  int elapsedSec, int contentLen)
{
    // 更新详细日志
    QTextCursor cursor = m_progressLog->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    QString lastLine = cursor.selectedText();
    if (lastLine.contains("|") && lastLine.contains("等待平台回复")) {
        cursor.removeSelectedText();
        cursor.deletePreviousChar();
    } else {
        cursor.movePosition(QTextCursor::End);
    }

    QString anim;
    int phase = (elapsedSec / 2) % 4;
    if (phase == 0) anim = "|";
    else if (phase == 1) anim = "/";
    else if (phase == 2) anim = "—";
    else anim = "\\";

    m_progressLog->append(
        QString("  %1 %2 | %3s | %4 字符 | 等待平台回复...")
            .arg(anim, stage).arg(elapsedSec).arg(contentLen));
    m_progressLog->verticalScrollBar()->setValue(
        m_progressLog->verticalScrollBar()->maximum());

    // 更新 StageProgress
    int idx = (stage == "synthesis") ? 1 : 0;
    StageProgress::Stage s;
    s.name = stage;
    s.label = QString("%1 %2").arg(stage == "synthesis" ? "整合" : "搜索", platform);
    s.state = StageProgress::Running;
    s.elapsedSec = elapsedSec;
    s.contentLen = contentLen;
    m_stageProgress->updateStage(idx, s);
}

void SearchPage::onStageFinished(const QString &stage, const QString &platform,
                                 int contentLen)
{
    if (contentLen > 0 || stage == "synthesis") {
        m_progressLog->append(
            QString("  ✓ %1 | %2 字符").arg(stage, QString::number(contentLen)));
    } else {
        m_progressLog->append(
            QString("  △ %1 | 已跳过").arg(stage));
    }
    m_progressLog->verticalScrollBar()->setValue(
        m_progressLog->verticalScrollBar()->maximum());

    // 更新 StageProgress
    int idx = (stage == "synthesis") ? 1 : 0;
    StageProgress::Stage s;
    s.name = stage;
    s.label = QString("%1 %2").arg(stage == "synthesis" ? "整合" : "搜索", platform);
    s.state = (contentLen > 0 || stage == "synthesis") ? StageProgress::Done : StageProgress::Skipped;
    s.contentLen = contentLen;
    m_stageProgress->updateStage(idx, s);
}

void SearchPage::onSearchFinished(bool success, const QString &reportPath)
{
    setSearchEnabled(true);
    m_statusAnimTimer->stop();
    m_historyList->clearSelection();

    if (success) {
        loadReport(reportPath);
        // 状态灯：绿色完成 → 2 秒后变灰
        m_statusIndicator->setText("◎");
        m_statusIndicator->setStyleSheet(QString("color: %1; font-size: 18px;").arg(Theme::Green));
        m_statusIndicator->setToolTip("搜索完成");
        QTimer::singleShot(2000, this, [this]() {
            if (m_statusIndicator->toolTip() == "搜索完成") {
                m_statusIndicator->setText("○");
                m_statusIndicator->setStyleSheet(QString("color: %1; font-size: 18px;").arg(Theme::TextMuted));
                m_statusIndicator->setToolTip("空闲");
            }
        });

        // === 缓存系统：搜索完成后查询缓存，为下次搜索做准备 ===
        m_lastSearchedQuery = m_queryInput->text().trimmed();
        m_cacheQueried = false;
        m_cachedMatches = QJsonArray();
        if (m_agent && m_db) {
            QString project = m_db->loadConfig("current_project", "");
            m_agent->sendCacheQuery(m_lastSearchedQuery, project, 3, 0.85);
        }
    } else {
        m_reportView->setHtml(
            "<div style='color:#f44336; text-align:center; margin-top:40px;'>"
            "<p style='font-size:18px;'>搜索未完成</p>"
            "<p style='color:#aaa;'>请查看上方进度日志了解失败原因</p>"
            "<p style='color:#888;'>常见原因: API Key 未配置 / 浏览器未就绪 / 平台会话过期</p></div>");
        m_statusIndicator->setText("✕");
        m_statusIndicator->setStyleSheet(QString("color: %1; font-size: 18px;").arg(Theme::Error));
        m_statusIndicator->setToolTip("搜索失败");
    }
    refreshHistory();
}

// ============================================================
// 缓存系统
// ============================================================

void SearchPage::onCacheHit(const QJsonArray &matches)
{
    // 只接受与上次搜索问题匹配的缓存结果
    if (!matches.isEmpty() && m_agent && m_agent->state() == AgentManager::Idle) {
        m_cachedMatches = matches;
        m_cacheQueried = true;
        qInfo() << "[SearchPage] 缓存命中:" << matches.size() << "条";
    }
}

void SearchPage::onCacheMiss()
{
    m_cachedMatches = QJsonArray();
    m_cacheQueried = true;
}

void SearchPage::onErrorOccurred(const QString &errorType, const QString &platform,
                                 const QString &detail)
{
    m_progressLog->append(
        QString("<span style='color:%1;'>✗ 错误 [%2] %3: %4</span>")
            .arg(Theme::Error, platform, errorType, detail));

    // 致命错误同时显示在报告区
    if (errorType == "handshake_timeout" || errorType == "execution_failed"
        || errorType == "heartbeat_timeout") {
        m_reportView->setHtml(
            QString("<div style='color:#f44336; text-align:center; margin-top:40px;'>"
                    "<p style='font-size:18px;'>搜索异常终止</p>"
                    "<p>类型: %1 | 平台: %2</p>"
                    "<p style='color:#aaa;'>%3</p></div>")
                .arg(errorType, platform, detail));
        setSearchEnabled(true);
    }
}

// ============================================================
// 报告加载
// ============================================================

void SearchPage::loadReport(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_reportView->setHtml(
            "<div style='color:#f44336;'>无法加载报告文件</div>");
        return;
    }
    QTextStream in(&file);
    QString markdown = in.readAll();
    file.close();

    QString html = markdownToHtml(markdown);
    m_reportView->setHtml(html);
}

// ============================================================
// 历史记录
// ============================================================

void SearchPage::refreshHistory()
{
    if (!m_db) return;
    m_historyList->clear();

    // 读取当前项目，按项目过滤历史
    QString currentProject = m_db->loadConfig("current_project", "");
    QString filter = m_historyFilter ? m_historyFilter->text().trimmed() : QString();

    QList<Database::SearchRecord> searches;
    // 有项目时只显示该项目记录，无项目时显示未归类记录
    if (!currentProject.isEmpty())
        searches = m_db->recentSearches(50, filter, currentProject);
    else
        searches = m_db->recentSearches(50, filter, "");  // 空 project

    for (const auto &s : searches) {
        QString statusIcon = (s.status == "done") ? "✓" : "✗";
        QString label = QString("[%1] %2%3  |  %4")
                            .arg(s.startedAt.left(10), statusIcon,
                                 s.query.left(38), s.platform);
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, s.reportPath);
        if (s.status != "done") {
            item->setForeground(QColor("#f44336"));
        }
        m_historyList->addItem(item);
    }
}

void SearchPage::onHistoryItemClicked(QListWidgetItem *item)
{
    QString reportPath = item->data(Qt::UserRole).toString();
    if (!reportPath.isEmpty()) loadReport(reportPath);
}

// ============================================================
// Markdown → HTML（简易，MVP 版本）
// ============================================================

static QString markdownToHtml(QString md)
{
    QString html;
    html += R"(<html><head><meta charset='utf-8'><style>
            * { margin:0; padding:0; box-sizing:border-box; }
            body {
              font-family: 'Segoe UI', 'PingFang SC', 'Microsoft YaHei', sans-serif;
              color: #c8d6e5; background: #161b22; padding: 32px 40px;
              line-height: 1.8; font-size: 15px; max-width: 900px; margin: 0 auto;
            }
            h1 { font-size: 1.8em; color: #58a6ff; border-bottom: 1px solid #30363d;
                 padding-bottom: 12px; margin: 32px 0 16px; font-weight: 600; }
            h2 { font-size: 1.4em; color: #7ee787; margin: 28px 0 12px;
                 padding: 6px 0; font-weight: 600; }
            h3 { font-size: 1.15em; color: #d2a8ff; margin: 22px 0 8px; font-weight: 600; }
            h4 { font-size: 1.05em; color: #ffb74d; margin: 18px 0 6px; font-weight: 600; }
            p { margin: 12px 0; }
            a { color: #58a6ff; text-decoration: none; border-bottom: 1px dotted #58a6ff33; }
            a:hover { color: #79c0ff; border-bottom-color: #79c0ff; }
            strong { color: #e6edf3; font-weight: 600; }
            em { color: #c8d6e5; font-style: italic; }
            del { color: #6e7681; text-decoration: line-through; }
            code { background: #1c2129; color: #f0883e; padding: 2px 6px;
                   border-radius: 4px; font-family: 'Cascadia Code','Consolas','JetBrains Mono',monospace;
                   font-size: 0.9em; }
            pre { background: #0d1117; padding: 16px 20px; border-radius: 6px;
                  border: 1px solid #30363d; overflow-x: auto; margin: 12px 0;
                  font-family: 'Cascadia Code','Consolas','JetBrains Mono',monospace;
                  font-size: 13px; line-height: 1.5; }
            pre code { background: none; color: #c8d6e5; padding: 0;
                       font-size: inherit; border-radius: 0; }
            blockquote { border-left: 3px solid #58a6ff; padding: 8px 16px;
                         margin: 16px 0; color: #8b949e;
                         background: #1c2129; border-radius: 0 4px 4px 0; }
            ul, ol { padding-left: 24px; margin: 8px 0; }
            li { margin: 4px 0; padding: 2px 0; }
            li::marker { color: #58a6ff; }
            hr { border: none; border-top: 1px solid #30363d; margin: 24px 0; }
            table { border-collapse: collapse; width: 100%; margin: 16px 0;
                    font-size: 14px; }
            th { background: #1c2129; color: #e6edf3; font-weight: 600;
                 padding: 10px 14px; text-align: left;
                 border: 1px solid #30363d; }
            td { padding: 8px 14px; border: 1px solid #30363d;
                 color: #c8d6e5; }
            tr:nth-child(even) td { background: #0d1117; }
            tr:hover td { background: #1c2533; }
            img { max-width: 100%; border-radius: 6px; margin: 12px 0; }
            .code-block { margin: 14px 0; }
            .code-lang { display: inline-block; background: #30363d; color: #8b949e;
                         font-size: 11px; padding: 4px 12px; border-radius: 6px 6px 0 0;
                         font-family: 'Consolas',monospace; text-transform: uppercase;
                         letter-spacing: 0.5px; }
            .code-lang + pre { margin-top: 0; border-radius: 0 6px 6px 6px; }
            .report-footer { margin-top: 40px; padding-top: 16px;
                             border-top: 1px solid #30363d; color: #6e7681;
                             font-size: 12px; text-align: center; }
            </style></head><body>)";

    const QStringList lines = md.split('\n');
    bool inCodeBlock = false;
    bool inTable = false;
    QStringList tableLines;
    QString codeBlock;
    QString codeLang;

    for (const QString &l : lines) {
        // 代码块
        if (l.startsWith("```")) {
            if (inTable) { finishTable(html, tableLines); inTable = false; }
            if (inCodeBlock) {
                html += "<div class='code-block'>";
                if (!codeLang.isEmpty())
                    html += "<div class='code-lang'>" + codeLang.toHtmlEscaped() + "</div>";
                html += "<pre><code>" + codeBlock.toHtmlEscaped() + "</code></pre></div>";
                codeBlock.clear();
                codeLang.clear();
                inCodeBlock = false;
            } else {
                inCodeBlock = true;
                codeLang = l.mid(3).trimmed();  // ```python → "python"
            }
            continue;
        }
        if (inCodeBlock) {
            if (!codeBlock.isEmpty()) codeBlock += '\n';
            codeBlock += l;
            continue;
        }

        // 表格检测：以 | 开头和结尾的行
        bool isTableLine = l.trimmed().startsWith('|') && l.trimmed().endsWith('|');
        // 分隔行：|---|----|
        bool isSepLine = l.trimmed().contains(QRegularExpression("^\\|[\\s\\-:]+\\|"));

        if (isTableLine && !isSepLine) {
            if (!inTable) { inTable = true; tableLines.clear(); }
            tableLines << l;
            continue;
        }
        if (isSepLine && (inTable || isTableLine)) {
            inTable = true;
            continue;  // 跳过分隔行
        }
        if (inTable && !isTableLine) {
            finishTable(html, tableLines);
            inTable = false;
        }

        if (l.startsWith("# ")) {
            html += "<h1>" + l.mid(2).toHtmlEscaped() + "</h1>";
        } else if (l.startsWith("## ")) {
            html += "<h2>" + l.mid(3).toHtmlEscaped() + "</h2>";
        } else if (l.startsWith("### ")) {
            html += "<h3>" + l.mid(4).toHtmlEscaped() + "</h3>";
        } else if (l.startsWith("#### ")) {
            html += "<h4>" + l.mid(5).toHtmlEscaped() + "</h4>";
        } else if (l.trimmed().startsWith("- ") || l.trimmed().startsWith("* ")) {
            html += "<li>" + inlineMarkdown(l.trimmed().mid(2)) + "</li>";
        } else if (l.trimmed().contains(QRegularExpression("^\\d+\\.\\s"))) {
            // 有序列表: "1. text"
            int dot = l.trimmed().indexOf('.');
            html += "<li>" + inlineMarkdown(l.trimmed().mid(dot + 1).trimmed()) + "</li>";
        } else if (l.startsWith("> ")) {
            html += "<blockquote>" + l.mid(2).toHtmlEscaped() + "</blockquote>";
        } else if (l.trimmed() == "---" || l.trimmed() == "***") {
            html += "<hr>";
        } else if (!l.isEmpty()) {
            html += "<p>" + inlineMarkdown(l) + "</p>";
        }
    }
    if (inTable) { finishTable(html, tableLines); }
    if (!codeBlock.isEmpty()) {
        html += "<pre><code>" + codeBlock.toHtmlEscaped() + "</code></pre>";
    }
    html += R"(<div class='report-footer'>Generated by Certus — AI Research System</div>)";
    html += "</body></html>";
    return html;
}

// 内联 Markdown 处理：粗体、斜体、行内代码、链接
static QString inlineMarkdown(const QString &text)
{
    QString p = text.toHtmlEscaped();
    p.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<strong>\\1</strong>");
    p.replace(QRegularExpression("\\*(.+?)\\*"), "<em>\\1</em>");
    p.replace(QRegularExpression("`(.+?)`"), "<code>\\1</code>");
    p.replace(QRegularExpression("!\\[([^\\]]*)\\]\\(([^)]+)\\)"),
              "<img src='\\2' alt='\\1' loading='lazy'>");
    p.replace(QRegularExpression("~~(.+?)~~"), "<del>\\1</del>");
    // 任务列表标记
    p.replace(QRegularExpression("^\\[x\\] "), "☑ ");
    p.replace(QRegularExpression("^\\[ \\] "), "☐ ");
    // 链接渲染（安全过滤危险协议）
    {
        QRegularExpression linkRe("\\[([^\\]]+)\\]\\(([^)]+)\\)");
        int offset = 0;
        QRegularExpressionMatch m;
        while ((m = linkRe.match(p, offset)).hasMatch()) {
            QString text2 = m.captured(1);
            QString href = m.captured(2);
            QString replacement;
            if (href.startsWith("javascript:", Qt::CaseInsensitive)
                || href.startsWith("data:", Qt::CaseInsensitive)
                || href.startsWith("vbscript:", Qt::CaseInsensitive)) {
                replacement = text2;
            } else {
                replacement = "<a href='" + href + "'>" + text2 + "</a>";
            }
            p.replace(m.capturedStart(), m.capturedLength(), replacement);
            offset = m.capturedStart() + replacement.length();
        }
    }
    return p;
}

// 表格渲染
static void finishTable(QString &html, const QStringList &lines)
{
    if (lines.isEmpty()) return;
    html += "<table>";
    bool firstRow = true;
    for (const QString &line : lines) {
        html += "<tr>";
        QStringList cells = line.trimmed().split('|');
        // 去掉首尾空元素（split 的首尾空白产生）
        int start = cells.isEmpty() ? 0 : (cells.first().trimmed().isEmpty() ? 1 : 0);
        int end = cells.size() - (cells.isEmpty() ? 0 : (cells.last().trimmed().isEmpty() ? 1 : 0));
        for (int i = start; i < end; i++) {
            QString tag = firstRow ? "th" : "td";
            html += "<" + tag + ">" + cells[i].trimmed().toHtmlEscaped() + "</" + tag + ">";
        }
        html += "</tr>";
        firstRow = false;
    }
    html += "</table>";
}

// ============================================================
// 状态指示器动画
// ============================================================

void SearchPage::updateStatusAnimation()
{
    static const QStringList spinChars = {"◐", "◓", "◑", "◒"};
    m_statusAnimFrame = (m_statusAnimFrame + 1) % spinChars.size();
    m_statusIndicator->setText(spinChars[m_statusAnimFrame]);
}

