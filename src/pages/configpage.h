#pragma once
#include <QWidget>
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QJsonObject>
#include <QMap>

class Database;
class BrowserManager;

class ConfigPage : public QWidget {
    Q_OBJECT
public:
    explicit ConfigPage(QWidget *parent = nullptr);
    void setDatabase(Database *db);
    void setBrowserManager(BrowserManager *bm);

private slots:
    void onSaveConfig();
    void onLoadConfig();
    void onAddProject();
    void onDeleteProject();
    void onSetCurrentProject();
    void onSavePlatforms();
    void onProjectSelected(QListWidgetItem *item);
    void onSaveSessionUrls();
    void onDetectEnvironment();
    void onStartTyping();
    void onLaunchBrowser();

private:
    void setupUi();
    void setupGeneralTab(QTabWidget *tabs);
    void setupProjectTab(QTabWidget *tabs);
    void setupPlatformTab(QTabWidget *tabs);
    bool loadConfigFromSQLite();
    void refreshProjectList();

    Database *m_db = nullptr;
    BrowserManager *m_browser = nullptr;
    QTabWidget *m_tabs = nullptr;

    // 常规配置
    QLineEdit *m_cdpPort = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QLabel *m_apiKeyStatus = nullptr;
    QLineEdit *m_apiUrl = nullptr;
    QLineEdit *m_claudeKey = nullptr;
    QLabel *m_claudeKeyStatus = nullptr;
    QLineEdit *m_codexKey = nullptr;
    QLabel *m_codexKeyStatus = nullptr;
    QComboBox *m_defaultSearchPlatform = nullptr;
    QComboBox *m_defaultSynthesisPlatform = nullptr;
    QCheckBox *m_autoDepth = nullptr;
    QCheckBox *m_autoLaunchBrowser = nullptr;

    // 项目管理
    QListWidget *m_projectList = nullptr;
    QLineEdit *m_newProjectName = nullptr;
    QLabel *m_currentProjectLabel = nullptr;
    QWidget *m_sessionUrlArea = nullptr;      // 平台链接编辑区（动态生成）
    QMap<QString, QLineEdit *> m_sessionEdits; // 平台名 → 链接输入框

    // 平台注册
    QTableWidget *m_platformTable = nullptr;
    QComboBox *m_typingPlatform = nullptr;
    QCheckBox *m_typingText = nullptr;
    QCheckBox *m_typingFile = nullptr;

    // 平台健康
    QLabel *m_healthDp = nullptr;     // deepseek
    QLabel *m_healthKi = nullptr;     // kimi
    QLabel *m_healthCg = nullptr;     // chatgpt
    QLabel *m_healthGm = nullptr;     // gemini
    QTimer *m_healthTimer = nullptr;

    // 日志级别
    QComboBox *m_logLevelCombo = nullptr;

    // 状态刷新
    void refreshPlatformHealth();
    void refreshTypingDropdown();
    void onLogLevelChanged(int index);

    // config.json 缓存
    QJsonObject m_configJson;
};
