#pragma once
/**
 * 会话搜索页 —— Certus 主页面。
 *
 * 布局：
 *   搜索区（输入框 + 深度/平台选择 + 按钮 + 状态指示器）
 *   进度区（阶段事件列表 + 进化五阶段进度）
 *   报告区（QTextBrowser Markdown 渲染）
 *   历史区（最近搜索列表 + 搜索框）
 */

#include <QWidget>
#include <QLineEdit>
#include <QRadioButton>
#include <QComboBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QListWidget>
#include <QTextEdit>
#include <QSplitter>
#include <QLabel>
#include <QTimer>
#include <QJsonArray>

class AgentManager;
class Database;
class BrowserManager;
class StageProgress;

class SearchPage : public QWidget {
    Q_OBJECT

public:
    explicit SearchPage(QWidget *parent = nullptr);

    void setAgentManager(AgentManager *agent);
    void setDatabase(Database *db);
    void setBrowserManager(BrowserManager *browser);

public slots:
    void onStageStarted(const QString &stage, const QString &question,
                        const QString &platform);
    void onStageFinished(const QString &stage, const QString &platform,
                         int contentLen);
    void onStageProgress(const QString &stage, const QString &platform,
                         int elapsedSec, int contentLen);
    void onSearchFinished(bool success, const QString &reportPath);
    void onErrorOccurred(const QString &errorType, const QString &platform,
                         const QString &detail);

    // 缓存系统
    void onCacheHit(const QJsonArray &matches);
    void onCacheMiss();

    // 状态刷新（MainWindow 切页时调用）
    void checkConfigStatus();

private slots:
    void onStartSearch();
    void onHistoryItemClicked(QListWidgetItem *item);
    void onHistoryContextMenu(const QPoint &pos);
    void loadReport(const QString &path);
    void updateStatusAnimation();

private:
    void setupUi();
    void refreshHistory();
    void setSearchEnabled(bool enabled);
    void updateBrowserStatus();
    void logWarning(const QString &msg);
    void logError(const QString &msg);

    AgentManager *m_agent = nullptr;
    Database *m_db = nullptr;
    BrowserManager *m_browser = nullptr;

    // 搜索区
    QLabel *m_brandTitle = nullptr;        // 品牌标题
    QLineEdit *m_queryInput = nullptr;
    QRadioButton *m_l2Radio = nullptr;
    QRadioButton *m_l3Radio = nullptr;
    QLabel *m_searchPlatformLabel = nullptr;
    QLabel *m_synthesisPlatformLabel = nullptr;
    QPushButton *m_searchButton = nullptr;

    // 状态指示器（五态灯）
    QLabel *m_statusIndicator = nullptr;
    QTimer *m_statusAnimTimer = nullptr;
    int m_statusAnimFrame = 0;

    // 进度区
    StageProgress *m_stageProgress = nullptr;  // 可视化阶段进度
    QTextEdit *m_progressLog = nullptr;        // 详细日志
    QPushButton *m_toggleLogBtn = nullptr;     // 展开/收起日志

    // 报告区
    QTextBrowser *m_reportView = nullptr;

    // 状态栏
    QLabel *m_configStatus = nullptr;
    QTimer *m_configTimer = nullptr;
    QLabel *m_browserStatus = nullptr;

    // 历史区（可折叠）
    QLabel *m_historyToggle = nullptr;     // 点击折叠/展开
    QLineEdit *m_historyFilter = nullptr;
    QListWidget *m_historyList = nullptr;
    bool m_historyExpanded = false;

    // 缓存系统
    QString m_lastSearchedQuery;        // 上次搜索的问题
    QJsonArray m_cachedMatches;          // 缓存命中结果（来自上次搜索后的查询）
    bool m_cacheQueried = false;         // 是否已查询过缓存
};
