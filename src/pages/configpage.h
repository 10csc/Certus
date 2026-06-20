#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QJsonObject>

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
    void onDetectEnvironment();
    void onLaunchBrowser();

private:
    void setupUi();
    bool loadConfigFromSQLite();

    Database *m_db = nullptr;
    BrowserManager *m_browser = nullptr;

    // 常规配置
    QLineEdit *m_cdpPort = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QLabel *m_apiKeyStatus = nullptr;
    QLineEdit *m_apiUrl = nullptr;
    QComboBox *m_defaultSearchPlatform = nullptr;
    QComboBox *m_defaultSynthesisPlatform = nullptr;
    QCheckBox *m_autoDepth = nullptr;
    QCheckBox *m_autoLaunchBrowser = nullptr;
    QLabel *m_envStatus = nullptr;

    // 状态刷新
    void refreshSearchPlatformDropdowns();
    void onLogLevelChanged(int index);

    // 日志级别
    QComboBox *m_logLevelCombo = nullptr;

    // config.json 缓存
    QJsonObject m_configJson;
};
