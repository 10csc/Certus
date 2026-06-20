#include "mainwindow.h"
#include "core/agentmanager.h"
#include "core/database.h"
#include "core/browsermanager.h"
#include "pages/searchpage.h"
#include "pages/configpage.h"
#include "pages/monitorpage.h"
#include "pages/memorypage.h"
#include "pages/repairpage.h"
#include "pages/platformpage.h"
#include "ui/theme.h"
#include "ui/icons.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStatusBar>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupStatusBar();
    setupSidebarFooter();

    // 状态刷新定时器
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatusIndicator);
    m_statusTimer->start(500);

    // Braille spinner 定时器
    m_spinnerTimer = new QTimer(this);
    connect(m_spinnerTimer, &QTimer::timeout, this, &MainWindow::updateSpinnerAnimation);
    m_spinnerTimer->setInterval(500);

    resize(1280, 720);
    setWindowTitle("Certus — 独立通用 AI 研究系统");
}

MainWindow::~MainWindow() = default;

void MainWindow::setAgentManager(AgentManager *agent)
{
    m_agent = agent;
    if (m_searchPage) {
        m_searchPage->setAgentManager(agent);
    }

    if (agent) {
        connect(agent, &AgentManager::searchStarted,
                this, &MainWindow::onSearchStarted);
        connect(agent, &AgentManager::searchFinished,
                this, &MainWindow::onSearchFinished);
        connect(agent, &AgentManager::stateChanged,
                this, &MainWindow::onStateChanged);
    }
}

void MainWindow::setDatabase(Database *db)
{
    m_db = db;
    if (m_searchPage) m_searchPage->setDatabase(db);
    if (m_configPage) m_configPage->setDatabase(db);
    if (m_monitorPage) m_monitorPage->setDatabase(db);
    if (m_memoryPage) m_memoryPage->setDatabase(db);
    if (m_repairPage) m_repairPage->setDatabase(db);
    if (m_platformPage) m_platformPage->setDatabase(db);
    updatePermanentStatus();
}

void MainWindow::setMemoryConfig(const QString &pythonPath, const QString &agentDir)
{
    if (m_memoryPage) {
        m_memoryPage->setPythonPath(pythonPath);
        m_memoryPage->setAgentDir(agentDir);
    }
}

void MainWindow::setBrowserManager(BrowserManager *browser)
{
    m_browser = browser;
    if (m_searchPage) m_searchPage->setBrowserManager(browser);
    if (m_configPage) m_configPage->setBrowserManager(browser);
    if (m_platformPage) m_platformPage->setBrowserManager(browser);
    updatePermanentStatus();
}

// ============================================================
// UI 布局
// ============================================================

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 侧边栏容器
    m_sidebarContainer = new QWidget(this);
    m_sidebarContainer->setFixedWidth(72);
    auto *sidebarLayout = new QVBoxLayout(m_sidebarContainer);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    // 侧边栏
    m_sidebar = new QListWidget(m_sidebarContainer);
    m_sidebar->setFixedWidth(72);
    m_sidebar->setIconSize(QSize(24, 24));
    m_sidebar->setSpacing(4);
    m_sidebar->setProperty("objectName", "sidebar");
    m_sidebar->setFont(QFont("Segoe UI", 10));
    m_sidebar->setStyleSheet(
        QString("QListWidget#sidebar { background: %1; border: none; font-size: 11px; }"
                "QListWidget#sidebar::item { padding: 12px 6px; text-align: center; }"
                "QListWidget#sidebar::item:selected { background: %2; color: %3; "
                "border-left: 3px solid %4; }"
                "QListWidget#sidebar::item:hover:!selected { background: %5; }")
            .arg(Theme::BgTertiary, Theme::BgHover, Theme::Accent,
                 Theme::Accent, Theme::BgSecondary));

    // 侧边栏项目：搜索/配置/平台/记忆（SVG 图标）
    struct SidebarItem { QIcon icon; QString text; };
    const SidebarItem items[] = {
        {Icons::search(), "搜索"},
        {Icons::config(), "配置"},
        {Icons::platform(), "平台"},
        {Icons::memory(), "记忆"},
    };
    for (const auto &item : items) {
        auto *widget = new QListWidgetItem(item.icon, item.text);
        widget->setTextAlignment(Qt::AlignCenter);
        m_sidebar->addItem(widget);
    }
    connect(m_sidebar, &QListWidget::currentRowChanged,
            this, &MainWindow::onSidebarClicked);

    sidebarLayout->addWidget(m_sidebar, 1);

    // 页面容器
    m_pages = new QStackedWidget(this);

    m_searchPage = new SearchPage(this);
    m_configPage = new ConfigPage(this);
    m_monitorPage = new MonitorPage(this);
    m_platformPage = new PlatformPage(this);
    m_memoryPage = new MemoryPage(this);
    m_repairPage = new RepairPage(this);

    m_pages->addWidget(m_searchPage);    // index 0
    m_pages->addWidget(m_monitorPage);   // index 1（隐藏）
    m_pages->addWidget(m_configPage);    // index 2
    m_pages->addWidget(m_platformPage);  // index 3
    m_pages->addWidget(m_memoryPage);    // index 4
    m_pages->addWidget(m_repairPage);    // index 5（隐藏）

    layout->addWidget(m_sidebarContainer);
    layout->addWidget(m_pages);

    setCentralWidget(central);

    // 默认选中搜索页
    m_sidebar->setCurrentRow(0);
}

// ============================================================
// 状态栏
// ============================================================

void MainWindow::setupStatusBar()
{
    m_statusIcon = new QLabel("○", this);
    m_statusIcon->setStyleSheet(QString("color: %1; font-size: 16px; margin-left: 8px;").arg(Theme::TextMuted));

    m_statusText = new QLabel("空闲", this);
    m_statusText->setStyleSheet(QString("color: %1; margin-left: 4px;").arg(Theme::TextMuted));

    statusBar()->addWidget(m_statusIcon);
    statusBar()->addWidget(m_statusText);

    // 右侧永久状态
    auto labelStyle = QString("color: %1; font-size: 11px; padding: 0 8px;").arg(Theme::TextMuted);

    m_statusDb = new QLabel("DB: ✓", this);
    m_statusDb->setStyleSheet(labelStyle);
    statusBar()->addPermanentWidget(m_statusDb);

    m_statusBrowser = new QLabel("浏览器: --", this);
    m_statusBrowser->setStyleSheet(labelStyle);
    statusBar()->addPermanentWidget(m_statusBrowser);
}

void MainWindow::updateStatusIndicator()
{
    // 由 AgentManager 状态驱动
}

// ============================================================
// 页面路由
// ============================================================

void MainWindow::onSidebarClicked(int row)
{
    // 侧边栏：搜索/配置/平台/记忆 → 映射到 m_pages 实际索引
    static const int pageIndex[] = {0, 2, 3, 4};
    if (row < 0 || row >= static_cast<int>(sizeof(pageIndex) / sizeof(pageIndex[0])))
        return;
    int idx = pageIndex[row];
    m_pages->setCurrentIndex(idx);

    // 搜索页：刷新配置状态
    if (idx == 0 && m_searchPage) {
        m_searchPage->checkConfigStatus();
    }
    // 记忆页：刷新列表
    if (idx == 4 && m_memoryPage) {
        m_memoryPage->refresh();
    }
}

// ============================================================
// 搜索状态响应
// ============================================================

void MainWindow::onSearchStarted()
{
    m_spinnerFrame = 0;
    m_spinnerTimer->start();
    m_statusText->setText("正在搜索...");
    m_statusIcon->setStyleSheet(QString("color: %1; font-size: 16px; margin-left: 8px;").arg(Theme::Accent));
}

void MainWindow::onSearchFinished(bool success, const QString &)
{
    m_spinnerTimer->stop();

    if (success) {
        m_statusIcon->setText("✓");
        m_statusIcon->setStyleSheet(QString("color: %1; font-size: 16px; margin-left: 8px; font-weight: bold;").arg(Theme::Success));
        m_statusText->setText("搜索完成");
        QTimer::singleShot(2000, this, [this]() {
            m_statusIcon->setText("○");
            m_statusIcon->setStyleSheet(QString("color: %1; font-size: 16px; margin-left: 8px;").arg(Theme::TextMuted));
            m_statusText->setText("空闲");
        });
    } else {
        m_statusIcon->setText("✕");
        m_statusIcon->setStyleSheet(QString("color: %1; font-size: 16px; margin-left: 8px;").arg(Theme::Error));
        m_statusText->setText("搜索失败");
    }
}

void MainWindow::onStateChanged(int state)
{
    updatePermanentStatus();

    const QStringList colors = {Theme::TextMuted, Theme::Accent, Theme::Accent, Theme::Warning, Theme::Error};
    const QStringList texts = {"空闲", "握手中...", "搜索中...", "取消中...", "错误"};

    int idx = qBound(0, state, colors.size() - 1);
    if (state != 2) {
        m_spinnerTimer->stop();
        const QStringList icons = {"○", "●", "●", "◉", "✕"};
        m_statusIcon->setText(icons[idx]);
    }
    m_statusIcon->setStyleSheet(
        QString("color: %1; font-size: 16px; margin-left: 8px;").arg(colors[idx]));
    m_statusText->setText(texts[idx]);
}

// ============================================================
// 侧边栏底部 + Spinner + 永久状态
// ============================================================

void MainWindow::setupSidebarFooter()
{
    auto *versionLabel = new QLabel("v0.3.0", m_sidebarContainer);
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setStyleSheet(
        QString("color: %1; font-size: 10px; padding: 8px 0;").arg(Theme::TextMuted));

    auto *layout = qobject_cast<QVBoxLayout *>(m_sidebarContainer->layout());
    if (layout) {
        layout->addWidget(versionLabel);
    }
}

void MainWindow::updateSpinnerAnimation()
{
    static const QChar spinnerChars[] = {
        QChar(0x280B), QChar(0x2819), QChar(0x2839),
        QChar(0x283C), QChar(0x2834), QChar(0x2826)
    };
    m_spinnerFrame = (m_spinnerFrame + 1) % 6;
    m_statusIcon->setText(QString(spinnerChars[m_spinnerFrame]));
}

void MainWindow::updatePermanentStatus()
{
    if (m_db) {
        m_statusDb->setText(QString("<span style='color:%1;'>DB: ✓</span>").arg(Theme::Success));
    }
    if (m_browser) {
        int port = m_db ? m_db->loadConfig("cdp_port", "9223").toInt() : 9223;
        bool cdpOk = m_browser->isCdpAvailable(port);
        if (cdpOk) {
            m_statusBrowser->setText(QString("<span style='color:%1;'>浏览器: ✓</span>").arg(Theme::Success));
        } else {
            m_statusBrowser->setText(QString("<span style='color:%1;'>浏览器: ✗</span>").arg(Theme::Warning));
        }
    }
}
