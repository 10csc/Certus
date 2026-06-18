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

private:
    void setupUi();
    void setupStatusBar();

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

    // 后端
    AgentManager *m_agent = nullptr;
    Database *m_db = nullptr;
    BrowserManager *m_browser = nullptr;
};
