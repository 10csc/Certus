#include "mainwindow.h"
#include "core/agentmanager.h"
#include "core/database.h"
#include "core/browsermanager.h"
#include "pages/searchpage.h"
#include "pages/configpage.h"
#include "pages/monitorpage.h"
#include "pages/memorypage.h"
#include "pages/repairpage.h"

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

    // 状态刷新定时器
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatusIndicator);
    m_statusTimer->start(500);

    resize(1000, 680);
    setWindowTitle("Certus —— 独立通用 AI 研究系统");
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

    // 侧边栏
    m_sidebar = new QListWidget(this);
    m_sidebar->setFixedWidth(80);
    m_sidebar->setIconSize(QSize(24, 24));
    m_sidebar->setSpacing(8);
    m_sidebar->setStyleSheet(
        "QListWidget { background: #2c2c2c; color: #ccc; border: none; font-size: 11px; }"
        "QListWidget::item { padding: 10px 8px; text-align: center; }"
        "QListWidget::item:selected { background: #0078d4; color: white; }"
        "QListWidget::item:hover { background: #3c3c3c; }");

    // 侧边栏项目
    struct SidebarItem { QString icon; QString text; };
    const SidebarItem items[] = {
        {"\xF0\x9F\x94\x8D", "搜索"},    // 🔍
        {"\xF0\x9F\x93\x8A", "监控"},    // 📊
        {"\xE2\x9A\x99", "配置"},        // ⚙
        {"\xF0\x9F\xA7\xA0", "记忆"},    // 🧠
        {"\xF0\x9F\x94\xA7", "修复"},    // 🔧
    };
    for (const auto &item : items) {
        auto *widget = new QListWidgetItem(item.icon + "\n" + item.text);
        widget->setTextAlignment(Qt::AlignCenter);
        m_sidebar->addItem(widget);
    }
    connect(m_sidebar, &QListWidget::currentRowChanged,
            this, &MainWindow::onSidebarClicked);

    // 页面容器
    m_pages = new QStackedWidget(this);
    m_pages->setStyleSheet("background: #1e1e1e;");

    m_searchPage = new SearchPage(this);
    m_configPage = new ConfigPage(this);
    m_monitorPage = new MonitorPage(this);
    m_memoryPage = new MemoryPage(this);
    m_repairPage = new RepairPage(this);

    m_pages->addWidget(m_searchPage);   // index 0
    m_pages->addWidget(m_monitorPage);  // index 1
    m_pages->addWidget(m_configPage);   // index 2
    m_pages->addWidget(m_memoryPage);   // index 3
    m_pages->addWidget(m_repairPage);   // index 4

    layout->addWidget(m_sidebar);
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
    m_statusIcon->setStyleSheet("color: gray; font-size: 16px; margin-left: 8px;");

    m_statusText = new QLabel("空闲", this);
    m_statusText->setStyleSheet("color: #888; margin-left: 4px;");

    statusBar()->addWidget(m_statusIcon);
    statusBar()->addWidget(m_statusText);
    statusBar()->setStyleSheet("QStatusBar { background: #252525; color: #888; }");
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
    if (row >= 0 && row < m_pages->count()) {
        m_pages->setCurrentIndex(row);
        // 切换到搜索页时立即刷新状态（浏览器/配置）
        if (row == 0 && m_searchPage) {
            m_searchPage->checkConfigStatus();
        }
        // 切换到监控页时自动刷新数据
        if (row == 1 && m_monitorPage) {
            m_monitorPage->refresh();
        }
        // 切换到记忆页时刷新列表
        if (row == 3 && m_memoryPage) {
            m_memoryPage->refresh();
        }
    }
}

// ============================================================
// 搜索状态响应
// ============================================================

void MainWindow::onSearchStarted()
{
    m_statusIcon->setText("●");
    m_statusIcon->setStyleSheet("color: #0078d4; font-size: 16px; margin-left: 8px;");
    m_statusText->setText("正在搜索...");
}

void MainWindow::onSearchFinished(bool success, const QString &)
{
    if (success) {
        m_statusIcon->setText("◎");
        m_statusIcon->setStyleSheet("color: #4caf50; font-size: 16px; margin-left: 8px;");
        m_statusText->setText("搜索完成");
        // 2 秒后恢复
        QTimer::singleShot(2000, this, [this]() {
            m_statusIcon->setText("○");
            m_statusIcon->setStyleSheet("color: gray; font-size: 16px; margin-left: 8px;");
            m_statusText->setText("空闲");
        });
    } else {
        m_statusIcon->setText("✕");
        m_statusIcon->setStyleSheet("color: #f44336; font-size: 16px; margin-left: 8px;");
        m_statusText->setText("搜索失败");
    }
}

void MainWindow::onStateChanged(int state)
{
    // state 对应 AgentManager::State 枚举值
    // Idle=0, Handshaking=1, Searching=2, Cancelling=3, Error=4
    const QStringList icons = {"○", "●", "●", "◉", "✕"};
    const QStringList colors = {"gray", "#0078d4", "#0078d4", "#ff9800", "#f44336"};
    const QStringList texts = {"空闲", "握手中...", "搜索中...", "取消中...", "错误"};

    int idx = qBound(0, state, icons.size() - 1);
    m_statusIcon->setText(icons[idx]);
    m_statusIcon->setStyleSheet(
        QString("color: %1; font-size: 16px; margin-left: 8px;").arg(colors[idx]));
    m_statusText->setText(texts[idx]);
}
