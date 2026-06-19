#pragma once
/**
 * Certus 主窗口 —— 侧边栏 + QStackedWidget 页面路由。
 */

#include <QMainWindow>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QTimer>

class AgentManager;
class Database;
class BrowserManager;
class SearchPage;
class ConfigPage;
class MonitorPage;
class MemoryPage;
class RepairPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void setAgentManager(AgentManager *agent);
    void setDatabase(Database *db);
    void setBrowserManager(BrowserManager *browser);
    void setMemoryConfig(const QString &pythonPath, const QString &agentDir);

private slots:
    void onSidebarClicked(int row);
    void onSearchStarted();
    void onSearchFinished(bool success, const QString &reportPath);
    void onStateChanged(int state);
    void updateStatusIndicator();
    void updateSpinnerAnimation();
    void updatePermanentStatus();

private:
    void setupUi();
    void setupStatusBar();
    void setupSidebarFooter();

    // 侧边栏 + 页面容器
    QListWidget *m_sidebar = nullptr;
    QStackedWidget *m_pages = nullptr;

    // 5 个页面
    SearchPage *m_searchPage = nullptr;
    ConfigPage *m_configPage = nullptr;
    MonitorPage *m_monitorPage = nullptr;
    MemoryPage *m_memoryPage = nullptr;
    RepairPage *m_repairPage = nullptr;

    // 状态栏
    QLabel *m_statusIcon = nullptr;
    QLabel *m_statusText = nullptr;
    QTimer *m_statusTimer = nullptr;

    // 状态栏右侧永久控件
    QLabel *m_statusProject = nullptr;   // 项目名
    QLabel *m_statusDb      = nullptr;   // DB 状态
    QLabel *m_statusBrowser  = nullptr;  // 浏览器状态

    // Braille spinner
    QTimer *m_spinnerTimer = nullptr;
    int m_spinnerFrame = 0;
    static constexpr const char *s_spinnerChars = "\u280B\u2819\u2839\u283C\u2834\u2826"; // ⠋⠙⠹⠼⠴⠦

    // 侧边栏底部
    QWidget *m_sidebarContainer = nullptr;

    // 后端
    AgentManager *m_agent = nullptr;
    Database *m_db = nullptr;
    BrowserManager *m_browser = nullptr;
};
