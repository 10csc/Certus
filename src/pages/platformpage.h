#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QJsonObject>

class Database;
class BrowserManager;

class PlatformPage : public QWidget {
    Q_OBJECT
public:
    explicit PlatformPage(QWidget *parent = nullptr);
    void setDatabase(Database *db);
    void setBrowserManager(BrowserManager *bm);

private slots:
    void onSavePlatforms();
    void onStartTyping();

private:
    void setupUi();
    void refreshTypingDropdown();
    void refreshSearchPlatformDropdowns();

    Database *m_db = nullptr;
    BrowserManager *m_browser = nullptr;

    // 平台注册
    QTableWidget *m_platformTable = nullptr;
    QComboBox *m_typingPlatform = nullptr;
    QCheckBox *m_typingText = nullptr;
    QCheckBox *m_typingFile = nullptr;
    QPushButton *m_typingBtn = nullptr;
    QLabel *m_typingStatus = nullptr;
    QLabel *m_envStatus = nullptr;

    // config.json 缓存（platform_urls 等）
    QJsonObject m_configJson;
};
