#include "configpage.h"
#include "../core/database.h"
#include "../utils/crypto.h"
#include "../utils/logger.h"
#include "../core/browsermanager.h"
#include <QSet>
#include <QMessageBox>
#include <QProcess>
#include <QDir>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QHeaderView>
#include <QTimer>
#include <QProcess>

ConfigPage::ConfigPage(QWidget *parent) : QWidget(parent)
{
    setupUi();
}

void ConfigPage::setDatabase(Database *db)
{
    m_db = db;
    loadConfigFromSQLite();
    onLoadConfig();
    refreshProjectList();

    // 载入平台 URL
    QJsonObject urls = m_configJson["platform_urls"].toObject();
    QStringList keys = urls.keys();
    m_platformTable->setRowCount(keys.size());
    for (int i = 0; i < keys.size(); i++) {
        m_platformTable->setItem(i, 0, new QTableWidgetItem(keys[i]));
        m_platformTable->setItem(i, 1, new QTableWidgetItem(urls[keys[i]].toString()));
    }
    // 刷新定型下拉框
    refreshTypingDropdown();
}

void ConfigPage::setBrowserManager(BrowserManager *bm)
{
    m_browser = bm;
}

void ConfigPage::onLaunchBrowser()
{
    if (!m_browser) return;
    int port = m_cdpPort->text().toInt();
    if (port < 1 || port > 65535) {
        QMessageBox::warning(this, "端口错误", "请输入有效的端口号 (1-65535)");
        return;
    }
    if (m_browser->isRunning()) {
        QMessageBox::information(this, "提示", "浏览器已在运行中");
        return;
    }
    // 先持久化端口，确保 SearchPage 能读取到最新值
    if (m_db) m_db->saveConfig("cdp_port", QString::number(port));
    int result = m_browser->launch(port);
    if (result > 0) {
        QMessageBox::information(this, "提示",
            QString("浏览器已在端口 %1 启动\nCDP 地址: http://127.0.0.1:%1/json/version").arg(result));
    } else {
        QMessageBox::warning(this, "启动失败",
            QString("浏览器 CDP 端口 %1 未就绪。\n\n"
                    "常见原因:\n"
                    "1. Edge/Chrome 已在运行中(无调试端口) → 请先关闭所有浏览器窗口\n"
                    "2. 浏览器冷启动慢 → 请稍后重试\n\n"
                    "手动启动: msedge --remote-debugging-port=%1").arg(port));
    }
}

// ============================================================
// 配置加载（SQLite 为唯一权威源）
// ============================================================

bool ConfigPage::loadConfigFromSQLite()
{
    if (!m_db) return false;

    // 确保 SQLite 中有默认配置（首次启动 seed）
    m_db->seedDefaultConfig();

    // 从 SQLite 加载到 m_configJson（供 UI 显示）
    auto loadJson = [&](const QString &key, const QString &def) -> QJsonObject {
        QString val = m_db->loadConfig(key, def);
        QJsonDocument doc = QJsonDocument::fromJson(val.toUtf8());
        return doc.isObject() ? doc.object() : QJsonObject{};
    };

    m_configJson = QJsonObject{};
    m_configJson["platform_urls"] = loadJson("platform_urls",
        R"({"deepseek":"https://chat.deepseek.com/","kimi":"https://www.kimi.com/","chatgpt":"https://chatgpt.com/","gemini":"https://gemini.google.com/"})");
    m_configJson["chat_path_patterns"] = loadJson("chat_path_patterns",
        R"({"deepseek":["/a/chat/s/"],"kimi":["/chat/"],"chatgpt":["/c/"],"gemini":["/app/"]})");
    m_configJson["sessions"] = loadJson("sessions", "{}");
    m_configJson["current_project"] = m_db->loadConfig("current_project", "");
    m_configJson["cdp_port"] = m_db->loadConfig("cdp_port", "9223").toInt();
    m_configJson["search_platform"] = m_db->loadConfig("search_platform", "deepseek");
    m_configJson["synthesis_platform"] = m_db->loadConfig("synthesis_platform", "kimi");
    m_configJson["deepseek_api"] = m_db->loadConfig("deepseek_api", "https://api.deepseek.com/v1");
    m_configJson["session_mode"] = m_db->loadConfig("session_mode", "fixed");

    return true;
}

// ============================================================
// UI 布局
// ============================================================

void ConfigPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->setStyleSheet(
        "QTabWidget::pane { border:1px solid #444; background:#1e1e1e; }"
        "QTabBar::tab { background:#2c2c2c; color:#aaa; padding:8px 16px; }"
        "QTabBar::tab:selected { background:#333; color:#fff; }");

    setupGeneralTab(m_tabs);
    setupProjectTab(m_tabs);
    setupPlatformTab(m_tabs);

    layout->addWidget(m_tabs);
}

void ConfigPage::setupGeneralTab(QTabWidget *tabs)
{
    auto *tab = new QWidget();
    auto *form = new QFormLayout(tab);
    form->setSpacing(12);

    auto *cdpRow = new QHBoxLayout();
    m_cdpPort = new QLineEdit("9223", this);
    m_cdpPort->setStyleSheet("background:#333; color:#fff; border:1px solid #555; padding:6px;");
    cdpRow->addWidget(m_cdpPort, 1);
    auto *launchBtn = new QPushButton("启动浏览器", this);
    launchBtn->setStyleSheet(
        "QPushButton{background:#555;color:#ccc;border:none;"
        "border-radius:4px;padding:6px 14px;font-size:12px;}"
        "QPushButton:hover{background:#666;}");
    connect(launchBtn, &QPushButton::clicked, this, &ConfigPage::onLaunchBrowser);
    cdpRow->addWidget(launchBtn);
    form->addRow("CDP 端口:", cdpRow);

    m_apiKey = new QLineEdit(this);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setStyleSheet("background:#333; color:#fff; border:1px solid #555; padding:6px;");
    form->addRow("DeepSeek Key:", m_apiKey);

    m_apiKeyStatus = new QLabel("未配置", this);
    m_apiKeyStatus->setStyleSheet("color:#888;");
    form->addRow("状态:", m_apiKeyStatus);

    m_apiUrl = new QLineEdit("https://api.deepseek.com/v1", this);
    m_apiUrl->setStyleSheet("background:#333; color:#fff; border:1px solid #555; padding:6px;");
    form->addRow("API 端点:", m_apiUrl);

    // Claude API Key（用于辅助修复页）
    m_claudeKey = new QLineEdit(this);
    m_claudeKey->setEchoMode(QLineEdit::Password);
    m_claudeKey->setPlaceholderText("sk-ant-... (Anthropic Console 获取)");
    m_claudeKey->setStyleSheet("background:#333; color:#fff; border:1px solid #555; padding:6px;");
    form->addRow("Claude Key:", m_claudeKey);
    m_claudeKeyStatus = new QLabel("未配置", this);
    m_claudeKeyStatus->setStyleSheet("color:#888;");
    form->addRow("", m_claudeKeyStatus);

    // Codex / OpenAI Key（用于辅助修复页）
    m_codexKey = new QLineEdit(this);
    m_codexKey->setEchoMode(QLineEdit::Password);
    m_codexKey->setPlaceholderText("sk-... (OpenAI Platform 获取)");
    m_codexKey->setStyleSheet("background:#333; color:#fff; border:1px solid #555; padding:6px;");
    form->addRow("Codex Key:", m_codexKey);
    m_codexKeyStatus = new QLabel("未配置", this);
    m_codexKeyStatus->setStyleSheet("color:#888;");
    form->addRow("", m_codexKeyStatus);

    m_defaultSearchPlatform = new QComboBox(this);
    m_defaultSearchPlatform->addItems({"deepseek", "kimi", "chatgpt", "gemini"});
    m_defaultSearchPlatform->setStyleSheet(
        "QComboBox { background: #333; color: #fff; border: 1px solid #555; padding: 4px; }");
    form->addRow("默认搜索平台:", m_defaultSearchPlatform);

    m_defaultSynthesisPlatform = new QComboBox(this);
    m_defaultSynthesisPlatform->addItems({"kimi", "deepseek", "chatgpt", "gemini"});
    m_defaultSynthesisPlatform->setStyleSheet(
        "QComboBox { background: #333; color: #fff; border: 1px solid #555; padding: 4px; }");
    form->addRow("默认整合平台:", m_defaultSynthesisPlatform);

    auto *saveBtn = new QPushButton("保存配置", this);
    saveBtn->setStyleSheet("QPushButton{background:#0078d4;color:white;border:none;"
                           "border-radius:4px;padding:8px 20px;}");
    connect(saveBtn, &QPushButton::clicked, this, &ConfigPage::onSaveConfig);
    form->addRow("", saveBtn);

    // 自动分析深度
    m_autoDepth = new QCheckBox("自动分析问题等级 (开启后系统自动判断 L2/L3)", this);
    m_autoDepth->setStyleSheet("color:#ccc;");
    m_autoDepth->setChecked(true);
    form->addRow("", m_autoDepth);

    // 自动启动浏览器
    m_autoLaunchBrowser = new QCheckBox("启动程序时自动启动浏览器", this);
    m_autoLaunchBrowser->setStyleSheet("color:#ccc;");
    form->addRow("", m_autoLaunchBrowser);

    // 环境自举
    auto *envBtn = new QPushButton("自动检测环境", this);
    envBtn->setStyleSheet(
        "QPushButton{background:#555;color:#ccc;border:none;"
        "border-radius:4px;padding:8px 20px;}"
        "QPushButton:hover{background:#666;}");
    connect(envBtn, &QPushButton::clicked, this, &ConfigPage::onDetectEnvironment);
    form->addRow("环境自举:", envBtn);

    m_envStatus = new QLabel("点击「自动检测环境」检查系统就绪状态", this);
    m_envStatus->setStyleSheet("color:#888; font-size:11px; padding:4px 0;");
    m_envStatus->setWordWrap(true);
    form->addRow("", m_envStatus);

    // 日志级别
    m_logLevelCombo = new QComboBox(this);
    m_logLevelCombo->addItems({"ERROR", "WARNING", "INFO", "DEBUG"});
    m_logLevelCombo->setCurrentIndex(2);  // 默认 INFO
    m_logLevelCombo->setStyleSheet(
        "QComboBox { background: #333; color: #fff; border: 1px solid #555; padding: 4px; }");
    connect(m_logLevelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConfigPage::onLogLevelChanged);
    form->addRow("日志级别:", m_logLevelCombo);

    // === 平台健康面板 ===
    auto *healthGroup = new QGroupBox("平台状态", this);
    healthGroup->setStyleSheet(
        "QGroupBox { color: #aaa; border: 1px solid #444; border-radius: 4px; "
        "padding-top: 10px; margin-top: 8px; }");
    auto *healthLayout = new QFormLayout(healthGroup);
    healthLayout->setSpacing(4);

    static const char *hNames[] = {"deepseek", "kimi", "chatgpt", "gemini"};
    QLabel **hLabels[] = {&m_healthDp, &m_healthKi, &m_healthCg, &m_healthGm};
    for (int i = 0; i < 4; i++) {
        *hLabels[i] = new QLabel("检测中...", this);
        (*hLabels[i])->setStyleSheet("color: #888; font-size: 12px;");
        healthLayout->addRow(QString("%1:").arg(hNames[i]), *hLabels[i]);
    }

    form->addRow(healthGroup);

    // 10 秒刷新一次平台健康状态
    m_healthTimer = new QTimer(this);
    connect(m_healthTimer, &QTimer::timeout, this, &ConfigPage::refreshPlatformHealth);
    m_healthTimer->start(10000);

    // 初始检测
    QTimer::singleShot(500, this, &ConfigPage::refreshPlatformHealth);

    tabs->addTab(tab, "常规配置");
}

void ConfigPage::setupProjectTab(QTabWidget *tabs)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    m_currentProjectLabel = new QLabel("当前项目: (未设置)", this);
    m_currentProjectLabel->setStyleSheet("color:#4fc3f7; padding:4px;");
    layout->addWidget(m_currentProjectLabel);

    // 新增行
    auto *row = new QHBoxLayout();
    m_newProjectName = new QLineEdit(this);
    m_newProjectName->setPlaceholderText("输入新项目名称...");
    m_newProjectName->setStyleSheet(
        "background:#333; color:#fff; border:1px solid #555; padding:6px;");
    auto *addBtn = new QPushButton("新增", this);
    addBtn->setStyleSheet(
        "QPushButton{background:#0078d4;color:white;border:none;padding:6px 16px;}");
    connect(addBtn, &QPushButton::clicked, this, &ConfigPage::onAddProject);
    row->addWidget(m_newProjectName, 1);
    row->addWidget(addBtn);
    layout->addLayout(row);

    // 项目列表
    m_projectList = new QListWidget(this);
    m_projectList->setStyleSheet(
        "QListWidget{background:#252525;color:#aaa;border:1px solid #444;}"
        "QListWidget::item{padding:6px;}"
        "QListWidget::item:hover{background:#333;}"
        "QListWidget::item:selected{background:#0078d4;color:white;}");
    m_projectList->setMaximumHeight(120);
    connect(m_projectList, &QListWidget::itemClicked,
            this, &ConfigPage::onProjectSelected);
    layout->addWidget(m_projectList);

    // 操作按钮
    auto *btnRow = new QHBoxLayout();
    auto *delBtn = new QPushButton("删除选中", this);
    auto *setCurBtn = new QPushButton("设为当前", this);
    QString btnStyle =
        "QPushButton{background:#555;color:#ccc;border:none;padding:6px 16px;}"
        "QPushButton:hover{background:#666;}";
    delBtn->setStyleSheet(btnStyle);
    setCurBtn->setStyleSheet(btnStyle);
    connect(delBtn, &QPushButton::clicked, this, &ConfigPage::onDeleteProject);
    connect(setCurBtn, &QPushButton::clicked, this, &ConfigPage::onSetCurrentProject);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(setCurBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    // 平台链接编辑区
    m_sessionUrlArea = new QWidget(this);
    auto *sessLayout = new QVBoxLayout(m_sessionUrlArea);
    sessLayout->setContentsMargins(0, 8, 0, 0);
    auto *sessTitle = new QLabel("选中项目后编辑各平台会话链接:", this);
    sessTitle->setStyleSheet("color:#aaa; font-size:11px;");
    sessLayout->addWidget(sessTitle);
    auto *sessForm = new QFormLayout();
    sessForm->setSpacing(6);
    sessLayout->addLayout(sessForm);
    // 占位：实际平台输入框在 onProjectSelected 中动态创建
    sessLayout->addStretch();
    layout->addWidget(m_sessionUrlArea);

    tabs->addTab(tab, "项目管理");
}

void ConfigPage::setupPlatformTab(QTabWidget *tabs)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    // 操作按钮行
    auto *btnRow = new QHBoxLayout();
    auto *addPlatBtn = new QPushButton("+ 新增平台", this);
    addPlatBtn->setStyleSheet(
        "QPushButton{background:#388e3c;color:white;border:none;"
        "border-radius:4px;padding:6px 14px;font-size:12px;}"
        "QPushButton:hover{background:#43a047;}");
    connect(addPlatBtn, &QPushButton::clicked, this, [this]() {
        int row = m_platformTable->rowCount();
        m_platformTable->insertRow(row);
        m_platformTable->setItem(row, 0, new QTableWidgetItem("新平台"));
        m_platformTable->setItem(row, 1, new QTableWidgetItem("https://"));
        refreshTypingDropdown();
    });
    btnRow->addWidget(addPlatBtn);

    auto *delPlatBtn = new QPushButton("− 删除选中", this);
    delPlatBtn->setStyleSheet(
        "QPushButton{background:#c62828;color:white;border:none;"
        "border-radius:4px;padding:6px 14px;font-size:12px;}"
        "QPushButton:hover{background:#d32f2f;}");
    connect(delPlatBtn, &QPushButton::clicked, this, [this]() {
        int row = m_platformTable->currentRow();
        if (row >= 0) {
            QString name = m_platformTable->item(row, 0)
                ? m_platformTable->item(row, 0)->text() : "?";
            auto answer = QMessageBox::question(this, "确认删除",
                QString("确定删除平台「%1」吗？\n此操作不可恢复。").arg(name));
            if (answer == QMessageBox::Yes) {
                m_platformTable->removeRow(row);
                refreshTypingDropdown();
            }
        } else {
            QMessageBox::information(this, "提示", "请先选中要删除的平台行");
        }
    });
    btnRow->addWidget(delPlatBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    // 平台表格
    m_platformTable = new QTableWidget(0, 2, this);
    m_platformTable->setHorizontalHeaderLabels({"平台名称", "平台URL"});
    m_platformTable->setStyleSheet(
        "QTableWidget { background:#1e1e1e; color:#ccc; gridline-color:#444; "
        "border:1px solid #444; }"
        "QHeaderView::section { background:#333; color:#ccc; padding:6px; "
        "border:1px solid #444; }"
        "QTableWidget::item { padding:4px; }");
    m_platformTable->horizontalHeader()->setStretchLastSection(true);
    m_platformTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_platformTable, 1);

    auto *saveBtn = new QPushButton("保存平台配置", this);
    saveBtn->setStyleSheet(
        "QPushButton{background:#0078d4;color:white;border:none;"
        "border-radius:4px;padding:8px 20px;}");
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        onSavePlatforms();
        refreshTypingDropdown();
    });
    layout->addWidget(saveBtn);

    // 平台定型区
    auto *typingGroup = new QGroupBox("平台定型", this);
    typingGroup->setStyleSheet(
        "QGroupBox { color:#ccc; border:1px solid #444; border-radius:4px; "
        "margin-top:12px; padding-top:16px; }"
        "QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 4px; }");
    auto *typingForm = new QFormLayout(typingGroup);
    m_typingPlatform = new QComboBox(this);
    m_typingPlatform->setStyleSheet(
        "QComboBox{background:#333;color:#fff;border:1px solid #555;padding:4px;}");
    typingForm->addRow("目标平台:", m_typingPlatform);

    m_typingText = new QCheckBox("文字收发定型", this);
    m_typingText->setStyleSheet("color:#ccc;");
    m_typingText->setChecked(true);
    typingForm->addRow("", m_typingText);
    m_typingFile = new QCheckBox("文件上传定型", this);
    m_typingFile->setStyleSheet("color:#ccc;");
    typingForm->addRow("", m_typingFile);

    m_typingBtn = new QPushButton("开始定型", this);
    m_typingBtn->setStyleSheet(
        "QPushButton{background:#0078d4;color:white;border:none;"
        "border-radius:4px;padding:8px 20px;}"
        "QPushButton:hover{background:#0086f0;}"
        "QPushButton:disabled{background:#444;color:#888;}");
    connect(m_typingBtn, &QPushButton::clicked, this, &ConfigPage::onStartTyping);
    typingForm->addRow("", m_typingBtn);

    m_typingStatus = new QLabel("未定型", this);
    m_typingStatus->setStyleSheet("color:#888; font-size:11px; padding:4px 0;");
    typingForm->addRow("状态:", m_typingStatus);
    layout->addWidget(typingGroup);

    tabs->addTab(tab, "平台注册");
}

// ============================================================
// 项目操作
// ============================================================

void ConfigPage::refreshProjectList()
{
    m_projectList->clear();
    QJsonObject sessions = m_configJson["sessions"].toObject();
    for (const auto &key : sessions.keys()) {
        m_projectList->addItem(key);
    }
    QString current = m_configJson["current_project"].toString();
    m_currentProjectLabel->setText(
        QString("当前项目: %1").arg(current.isEmpty() ? "(未设置)" : current));
    // 同步到 SQLite（SearchPage / AgentManager 读取用）
    if (m_db && !current.isEmpty())
        m_db->saveConfig("current_project", current);
}

void ConfigPage::onProjectSelected(QListWidgetItem *item)
{
    if (!item) return;
    QString project = item->text();

    // 清空旧输入框
    m_sessionEdits.clear();
    QFormLayout *form = nullptr;
    // 找到 m_sessionUrlArea 中的 QFormLayout
    for (auto *child : m_sessionUrlArea->children()) {
        auto *lay = qobject_cast<QVBoxLayout *>(child);
        if (lay) {
            for (int i = 0; i < lay->count(); i++) {
                auto *item = lay->itemAt(i);
                if (item && item->layout()) {
                    form = qobject_cast<QFormLayout *>(item->layout());
                    break;
                }
            }
        }
    }

    // 清除旧行
    if (form) {
        while (form->rowCount() > 0)
            form->removeRow(0);
    }

    // 读取当前项目的会话链接
    QJsonObject sessions = m_configJson["sessions"].toObject();
    QJsonObject projSessions = sessions[project].toObject();
    QJsonObject platformUrls = m_configJson["platform_urls"].toObject();

    // 为每个注册平台创建输入框
    for (const auto &key : platformUrls.keys()) {
        QString platName = key;
        QString currentUrl = projSessions[platName].toString();
        auto *edit = new QLineEdit(currentUrl, m_sessionUrlArea);
        edit->setPlaceholderText(QString("输入 %1 的聊天链接...").arg(platName));
        edit->setStyleSheet(
            "background:#333; color:#fff; border:1px solid #555; padding:4px; font-size:11px;");
        m_sessionEdits[platName] = edit;
        if (form)
            form->addRow(QString("%1:").arg(platName), edit);
    }

    // 添加保存按钮
    if (form && !m_sessionEdits.isEmpty()) {
        auto *saveBtn = new QPushButton("保存会话链接", m_sessionUrlArea);
        saveBtn->setStyleSheet(
            "QPushButton{background:#0078d4;color:white;border:none;"
            "border-radius:3px;padding:4px 12px;font-size:11px;}");
        connect(saveBtn, &QPushButton::clicked, this, &ConfigPage::onSaveSessionUrls);
        form->addRow("", saveBtn);
    }
}

void ConfigPage::onSaveSessionUrls()
{
    auto *item = m_projectList->currentItem();
    if (!item) return;
    QString project = item->text();

    QJsonObject sessions = m_configJson["sessions"].toObject();
    QJsonObject projSessions = sessions[project].toObject();

    for (auto it = m_sessionEdits.begin(); it != m_sessionEdits.end(); ++it) {
        QString url = it.value()->text().trimmed();
        if (!url.isEmpty()) {
            projSessions[it.key()] = url;
        } else {
            projSessions.remove(it.key());
        }
    }

    sessions[project] = projSessions;
    m_configJson["sessions"] = sessions;
    // 同步到 SQLite（C++ hello 从此读取）
    if (m_db) {
        QJsonDocument sdoc(sessions);
        m_db->saveConfig("sessions", QString::fromUtf8(sdoc.toJson()));
    }
    QMessageBox::information(this, "提示", QString("项目「%1」的会话链接已保存").arg(project));
}

void ConfigPage::onAddProject()
{
    QString name = m_newProjectName->text().trimmed();
    if (name.isEmpty()) return;

    QJsonObject sessions = m_configJson["sessions"].toObject();
    if (sessions.contains(name)) {
        QMessageBox::information(this, "提示", "项目已存在");
        return;
    }
    sessions[name] = QJsonObject{};
    // 同步到 SQLite
    if (m_db) {
        QJsonDocument doc(sessions);
        m_db->saveConfig("sessions", QString::fromUtf8(doc.toJson()));
    }
    m_newProjectName->clear();
    refreshProjectList();
}

void ConfigPage::onDeleteProject()
{
    auto *item = m_projectList->currentItem();
    if (!item) return;
    QString name = item->text();

    auto result = QMessageBox::question(this, "确认删除",
        QString("确定删除项目「%1」？").arg(name));
    if (result != QMessageBox::Yes) return;

    QJsonObject sessions = m_configJson["sessions"].toObject();
    sessions.remove(name);

    // 同步到 SQLite
    if (m_db) {
        QJsonDocument doc(sessions);
        m_db->saveConfig("sessions", QString::fromUtf8(doc.toJson()));
        if (m_configJson["current_project"].toString() == name)
            m_db->saveConfig("current_project", "");
    }

    refreshProjectList();
}

void ConfigPage::onSetCurrentProject()
{
    auto *item = m_projectList->currentItem();
    if (!item) return;
    // 同步到 SQLite
    if (m_db) m_db->saveConfig("current_project", item->text());
    refreshProjectList();
}

// ============================================================
// 平台操作
// ============================================================

void ConfigPage::onSavePlatforms()
{
    QJsonObject urls;
    for (int i = 0; i < m_platformTable->rowCount(); i++) {
        auto *nameItem = m_platformTable->item(i, 0);
        auto *urlItem = m_platformTable->item(i, 1);
        if (nameItem && urlItem) {
            QString name = nameItem->text().trimmed();
            QString url = urlItem->text().trimmed();
            if (!name.isEmpty() && !url.isEmpty()) {
                urls[name] = url;
            }
        }
    }
    m_configJson["platform_urls"] = urls;
    // 同步到 SQLite（C++ hello 从此读取）
    if (m_db) {
        QJsonDocument doc(urls);
        m_db->saveConfig("platform_urls", QString::fromUtf8(doc.toJson()));
    }
    QMessageBox::information(this, "提示", "平台配置已保存");
}

// ============================================================
// 常规配置读写
// ============================================================

void ConfigPage::onSaveConfig()
{
    if (!m_db) return;
    m_db->saveConfig("cdp_port", m_cdpPort->text());
    m_db->saveConfig("deepseek_api", m_apiUrl->text());
    m_db->saveConfig("search_platform", m_defaultSearchPlatform->currentText());
    m_db->saveConfig("synthesis_platform", m_defaultSynthesisPlatform->currentText());
    m_db->saveConfig("auto_depth", m_autoDepth->isChecked() ? "1" : "0");
    m_db->saveConfig("auto_launch_browser", m_autoLaunchBrowser->isChecked() ? "1" : "0");
    m_db->saveConfig("session_mode", "fixed");

    // 首次保存时写入 chat_path_patterns 默认值（后续不覆盖用户自定义）
    if (m_db->loadConfig("chat_path_patterns", "").isEmpty()) {
        QJsonObject patterns;
        { QJsonArray a; a.append(QString("/a/chat/s/")); patterns["deepseek"] = a; }
        { QJsonArray a; a.append(QString("/chat/")); patterns["kimi"] = a; }
        { QJsonArray a; a.append(QString("/c/")); patterns["chatgpt"] = a; }
        { QJsonArray a; a.append(QString("/app/")); patterns["gemini"] = a; }
        QJsonDocument pdoc(patterns);
        m_db->saveConfig("chat_path_patterns", QString::fromUtf8(pdoc.toJson()));
    }

    // 首次保存时写入 platform_urls 默认值（后续不覆盖用户自定义）
    if (m_db->loadConfig("platform_urls", "").isEmpty()) {
        QJsonObject urls;
        urls["deepseek"] = "https://chat.deepseek.com/";
        urls["kimi"] = "https://www.kimi.com/";
        urls["chatgpt"] = "https://chatgpt.com/";
        urls["gemini"] = "https://gemini.google.com/";
        QJsonDocument udoc(urls);
        m_db->saveConfig("platform_urls", QString::fromUtf8(udoc.toJson()));
    }

    QString apiKey = m_apiKey->text();
    if (!apiKey.isEmpty()) {
        QString encrypted = Crypto::encrypt(apiKey);
        if (!encrypted.isEmpty()) {
            m_db->saveConfig("deepseek_key", encrypted);
            m_db->saveConfig("deepseek_key_hash", Crypto::sha256Prefix8(apiKey));
            m_apiKeyStatus->setText("已加密存储 ✓");
            m_apiKeyStatus->setStyleSheet("color:#4caf50;");
            m_apiKey->clear();
        }
    }

    // Claude Key
    QString claudeKey = m_claudeKey->text();
    if (!claudeKey.isEmpty()) {
        QString encrypted = Crypto::encrypt(claudeKey);
        if (!encrypted.isEmpty()) {
            m_db->saveConfig("claude_key", encrypted);
            m_db->saveConfig("claude_key_hash", Crypto::sha256Prefix8(claudeKey));
            m_claudeKeyStatus->setText("已加密存储 ✓");
            m_claudeKeyStatus->setStyleSheet("color:#4caf50;");
            m_claudeKey->clear();
        }
    }

    // Codex Key
    QString codexKey = m_codexKey->text();
    if (!codexKey.isEmpty()) {
        QString encrypted = Crypto::encrypt(codexKey);
        if (!encrypted.isEmpty()) {
            m_db->saveConfig("codex_key", encrypted);
            m_db->saveConfig("codex_key_hash", Crypto::sha256Prefix8(codexKey));
            m_codexKeyStatus->setText("已加密存储 ✓");
            m_codexKeyStatus->setStyleSheet("color:#4caf50;");
            m_codexKey->clear();
        }
    }

    // 反馈
    m_apiKeyStatus->setStyleSheet("color:#4caf50; font-weight:bold;");
    m_apiKeyStatus->setText("配置已保存 ✓");
    QTimer::singleShot(3000, this, [this]() {
        QString hash = m_db ? m_db->loadConfig("deepseek_key_hash") : QString();
        m_apiKeyStatus->setText(hash.isEmpty() ? "未配置" : "已配置 ✓");
        m_apiKeyStatus->setStyleSheet(hash.isEmpty() ? "color:#888;" : "color:#4caf50;");

        QString claudeHash = m_db ? m_db->loadConfig("claude_key_hash") : QString();
        m_claudeKeyStatus->setText(claudeHash.isEmpty() ? "未配置" : "已配置 ✓");
        m_claudeKeyStatus->setStyleSheet(claudeHash.isEmpty() ? "color:#888;" : "color:#4caf50;");

        QString codexHash = m_db ? m_db->loadConfig("codex_key_hash") : QString();
        m_codexKeyStatus->setText(codexHash.isEmpty() ? "未配置" : "已配置 ✓");
        m_codexKeyStatus->setStyleSheet(codexHash.isEmpty() ? "color:#888;" : "color:#4caf50;");
    });
}

void ConfigPage::onLoadConfig()
{
    if (!m_db) return;
    m_cdpPort->setText(m_db->loadConfig("cdp_port", "9223"));
    m_apiUrl->setText(m_db->loadConfig("deepseek_api", "https://api.deepseek.com/v1"));

    QString searchPlat = m_db->loadConfig("search_platform", "deepseek");
    int spIdx = m_defaultSearchPlatform->findText(searchPlat);
    if (spIdx >= 0) m_defaultSearchPlatform->setCurrentIndex(spIdx);

    QString synthPlat = m_db->loadConfig("synthesis_platform", "kimi");
    int kpIdx = m_defaultSynthesisPlatform->findText(synthPlat);
    if (kpIdx >= 0) m_defaultSynthesisPlatform->setCurrentIndex(kpIdx);

    bool autoLaunch = m_db->loadConfig("auto_launch_browser", "0") == "1";
    bool autoDepth = m_db->loadConfig("auto_depth", "1") == "1";
    m_autoDepth->setChecked(autoDepth);
    m_autoLaunchBrowser->setChecked(autoLaunch);

    QString hash = m_db->loadConfig("deepseek_key_hash");
    m_apiKeyStatus->setText(hash.isEmpty() ? "未配置" : "已配置 ✓");
    m_apiKeyStatus->setStyleSheet(hash.isEmpty() ? "color:#888;" : "color:#4caf50;");

    QString claudeHash = m_db->loadConfig("claude_key_hash");
    m_claudeKeyStatus->setText(claudeHash.isEmpty() ? "未配置" : "已配置 ✓");
    m_claudeKeyStatus->setStyleSheet(claudeHash.isEmpty() ? "color:#888;" : "color:#4caf50;");

    QString codexHash = m_db->loadConfig("codex_key_hash");
    m_codexKeyStatus->setText(codexHash.isEmpty() ? "未配置" : "已配置 ✓");
    m_codexKeyStatus->setStyleSheet(codexHash.isEmpty() ? "color:#888;" : "color:#4caf50;");
}

// ============================================================
// 环境自举
// ============================================================

void ConfigPage::onDetectEnvironment()
{
    if (!m_db) return;

    // 更新状态标签
    m_envStatus->setStyleSheet("color:#4fc3f7; font-size:11px; padding:4px 0;");
    m_envStatus->setText("● 检测中...");

    // 强制刷新 UI
    QCoreApplication::processEvents();

    QStringList okLines;
    QStringList warnLines;
    QStringList errLines;

    // 1. 检测 Python
    QStringList pythonCandidates = {
        "D:/python_all/Python312/python.exe",
        "C:/Python312/python.exe",
        "C:/Python311/python.exe",
        "python",
    };
    QString pythonPath;
    for (const auto &py : pythonCandidates) {
        QProcess proc;
        proc.start(py, {"--version"});
        if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
            QString ver = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            m_db->saveConfig("python_path", py);
            pythonPath = py;
            okLines.append(QString("✓ Python: %1").arg(ver));
            break;
        }
    }
    if (pythonPath.isEmpty())
        errLines.append("✗ Python: 未找到 (请安装 Python 3.12+)");

    // 2. 检测 Playwright
    if (!pythonPath.isEmpty()) {
        QProcess pwProc;
        pwProc.start(pythonPath, {"-c", "import playwright; print(playwright.__file__)"});
        if (pwProc.waitForFinished(5000) && pwProc.exitCode() == 0)
            okLines.append("✓ Playwright: 已安装");
        else
            warnLines.append("△ Playwright: 未安装 (运行 pip install playwright 后重试)");
    }

    // 3. 检测浏览器
    QString browserPath = BrowserManager::findBrowserPath(BrowserManager::Edge);
    if (browserPath.isEmpty())
        browserPath = BrowserManager::findBrowserPath(BrowserManager::Chrome);
    if (!browserPath.isEmpty())
        okLines.append(QString("✓ 浏览器: %1").arg(
            QFileInfo(browserPath).baseName()));
    else
        warnLines.append("△ 浏览器: 未找到 Edge/Chrome");

    // 4. 扫描 CDP 端口
    if (m_browser) {
        int port = m_browser->scanCdpPort(9223, 9226);
        if (port > 0) {
            okLines.append(QString("✓ CDP 端口: %1").arg(port));
            m_cdpPort->setText(QString::number(port));
        } else {
            warnLines.append(QString("△ CDP: 端口 %1 无响应 (请启动浏览器)").arg(9223));
        }
    }

    // 5. 检查 API Key
    QString encryptedKey = m_db->loadConfig("deepseek_key", "");
    if (!encryptedKey.isEmpty()) {
        QString decrypted = Crypto::decrypt(encryptedKey);
        if (!decrypted.isEmpty())
            okLines.append("✓ DeepSeek API Key: 已配置");
        else
            warnLines.append("△ API Key 解密失败");
    } else {
        warnLines.append("△ DeepSeek API Key: 未配置 (本地总结功能受限)");
    }

    // 6. 平台 URL 完整性
    QJsonObject urls = m_configJson["platform_urls"].toObject();
    if (urls.isEmpty())
        warnLines.append("△ 平台 URL: 未注册任何平台");
    else
        okLines.append(QString("✓ 平台 URL: 已注册 %1 个平台").arg(urls.size()));

    // 7. 会话链接
    QString currentProject = m_db->loadConfig("current_project", "");
    if (currentProject.isEmpty()) {
        warnLines.append("△ 当前项目: 未设置");
    } else {
        QString sessionsJson = m_db->loadConfig("sessions", "{}");
        QJsonObject sessions = QJsonDocument::fromJson(sessionsJson.toUtf8()).object();
        QJsonObject projSessions = sessions[currentProject].toObject();
        if (projSessions.isEmpty())
            warnLines.append(QString("△ 项目「%1」无会话链接").arg(currentProject));
        else
            okLines.append(QString("✓ 项目「%1」: %2 个会话链接").arg(
                currentProject).arg(projSessions.size()));
    }

    // 汇总显示
    QStringList allLines;
    allLines << okLines << warnLines << errLines;
    QString html = allLines.join("<br>");
    html.replace("✓", "<span style='color:#81c784;'>✓</span>");
    html.replace("✗", "<span style='color:#f44336;'>✗</span>");
    html.replace("△", "<span style='color:#ff9800;'>△</span>");

    // 整体状态
    bool allOk = errLines.isEmpty() && warnLines.isEmpty();
    QString overall = allOk ? "<span style='color:#81c784;'>● 环境就绪</span>"
                            : (errLines.isEmpty()
                               ? "<span style='color:#ff9800;'>● 环境基本可用 (部分警告)</span>"
                               : "<span style='color:#f44336;'>● 环境不可用 (需修复错误)</span>");
    m_envStatus->setText(overall + "<br>" + html);

    // 仍然显示弹窗供复制
    QString plain = (okLines + warnLines + errLines).join("\n");
    QMessageBox::information(this, "环境检测结果", plain.isEmpty() ? "一切正常" : plain);
}

// ============================================================
// 平台定型
// ============================================================

void ConfigPage::onStartTyping()
{
    QString plat = m_typingPlatform->currentText();
    QStringList targets;
    if (m_typingText->isChecked()) targets.append("文字收发");
    if (m_typingFile->isChecked()) targets.append("文件上传");
    if (targets.isEmpty()) {
        QMessageBox::warning(this, "定型", "请至少选择一个定型目标");
        return;
    }

    // 获取平台 URL
    QString platUrl;
    QJsonObject urls = m_configJson["platform_urls"].toObject();
    if (urls.contains(plat)) {
        platUrl = urls[plat].toString();
    } else {
        platUrl = QString("https://%1.com/").arg(plat);
    }

    // 获取 CDP 端口
    int cdpPort = m_cdpPort->text().toInt();
    if (cdpPort < 1) cdpPort = 9223;

    // 检查浏览器是否在运行
    if (!m_browser || !m_browser->isCdpAvailable(cdpPort)) {
        QMessageBox::warning(this, "定型",
            QString("浏览器未就绪 (端口 %1)。\n请先在「常规配置」中启动浏览器。").arg(cdpPort));
        return;
    }

    // 确认对话框
    auto answer = QMessageBox::question(this, "定型",
        QString("将对「%1」平台进行定型\nURL: %2\nCDP: %3\n定型目标: %4\n\n"
                "定型过程约需 60 秒，请保持浏览器窗口打开。\n继续？")
            .arg(plat, platUrl).arg(cdpPort).arg(targets.join(" + ")));
    if (answer != QMessageBox::Yes) return;

    // 启动 typing_agent.py
    QProcess *proc = new QProcess(this);
    QString agentDir = QDir::currentPath() + "/agent";
    QString typingPy = agentDir + "/typing_agent.py";

    QStringList args;
    args << typingPy << plat << platUrl
         << "--cdp-port" << QString::number(cdpPort);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUTF8", "1");
    proc->setProcessEnvironment(env);
    proc->setWorkingDirectory(agentDir);

    // UI 状态更新：定型中
    m_typingBtn->setEnabled(false);
    m_typingBtn->setText("定型中...");
    m_typingStatus->setText(QString("● 正在定型 %1 ...").arg(plat));
    m_typingStatus->setStyleSheet("color:#4fc3f7; font-size:11px; padding:4px 0;");

    // 完成处理
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, plat, proc](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        m_typingBtn->setEnabled(true);
        m_typingBtn->setText("开始定型");

        if (exitCode == 0) {
            m_typingStatus->setText(QString("✓ %1 定型成功").arg(plat));
            m_typingStatus->setStyleSheet(
                "color:#81c784; font-size:11px; font-weight:bold; padding:4px 0;");
            // 刷新定型下拉框
            refreshTypingDropdown();
        } else {
            QString errOutput = QString::fromUtf8(proc->readAllStandardError());
            QString detail = errOutput.isEmpty()
                ? QString("退出码 %1").arg(exitCode)
                : errOutput.left(120);
            m_typingStatus->setText(QString("✗ %1 定型失败: %2").arg(plat, detail));
            m_typingStatus->setStyleSheet(
                "color:#f44336; font-size:11px; padding:4px 0;");
        }
    });

    connect(proc, &QProcess::readyReadStandardOutput, this, [proc]() {
        qInfo().noquote() << "[Typing]" << proc->readAllStandardOutput().trimmed();
    });
    connect(proc, &QProcess::readyReadStandardError, this, [proc]() {
        qWarning().noquote() << "[Typing]" << proc->readAllStandardError().trimmed();
    });

    proc->start("python", args);
}

// ============================================================
// 平台健康刷新 + 日志级别
// ============================================================

void ConfigPage::refreshPlatformHealth()
{
    if (!m_browser) return;

    int cdpPort = m_cdpPort->text().toInt();
    bool cdpOk = m_browser->isCdpAvailable(cdpPort);

    static const QStringList platforms = {"deepseek", "kimi", "chatgpt", "gemini"};
    QLabel *labels[] = {m_healthDp, m_healthKi, m_healthCg, m_healthGm};

    for (int i = 0; i < platforms.size(); i++) {
        if (!labels[i]) continue;

        // 查询该平台最近性能
        double avgElapsed = 0;
        if (m_db) {
            auto perfs = m_db->platformPerformanceByPlatform(platforms[i]);
            if (!perfs.isEmpty()) {
                avgElapsed = perfs.first().avgElapsed;
            }
        }

        QString status;
        if (cdpOk) {
            status = QString("● 可达 | 平均 %1s")
                         .arg(avgElapsed > 0 ? QString::number(avgElapsed, 'f', 1)
                                            : QString("--"));
            labels[i]->setStyleSheet("color: #81c784; font-size: 12px;");
        } else {
            status = "○ CDP 未连接";
            labels[i]->setStyleSheet("color: #888; font-size: 12px;");
        }
        labels[i]->setText(QString("%1: %2").arg(platforms[i], status));
    }
}

void ConfigPage::onLogLevelChanged(int index)
{
    static const Logger::Level levels[] = {
        Logger::Error, Logger::Warning, Logger::Info, Logger::Debug
    };
    if (index >= 0 && index < 4) {
        Logger::instance()->setLevel(levels[index]);
        if (m_db) {
            m_db->saveConfig("log_level",
                             QString::number(static_cast<int>(levels[index])));
        }
    }
}

void ConfigPage::refreshTypingDropdown()
{
    if (!m_typingPlatform) return;

    // 收集当前所有已知平台（去重）
    QSet<QString> known;

    // 1. 从表格读取
    for (int i = 0; i < m_platformTable->rowCount(); i++) {
        auto *item = m_platformTable->item(i, 0);
        if (item && !item->text().trimmed().isEmpty())
            known.insert(item->text().trimmed());
    }

    // 2. 从 config.json 读取
    QJsonObject urls = m_configJson["platform_urls"].toObject();
    for (const auto &key : urls.keys())
        known.insert(key);

    // 3. 追加内置平台
    static const QStringList builtin = {"deepseek", "kimi", "chatgpt", "gemini"};
    for (const auto &p : builtin)
        known.insert(p);

    // 刷新下拉框
    QString current = m_typingPlatform->currentText();
    m_typingPlatform->clear();
    for (const auto &p : known)
        m_typingPlatform->addItem(p);

    // 恢复选中
    int idx = m_typingPlatform->findText(current);
    if (idx >= 0) m_typingPlatform->setCurrentIndex(idx);
}
