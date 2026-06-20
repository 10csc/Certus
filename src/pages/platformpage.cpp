#include "platformpage.h"
#include "../core/database.h"
#include "../utils/crypto.h"
#include "../core/browsermanager.h"
#include "../ui/theme.h"
#include "../ui/toast.h"
#include <QSet>
#include <QMessageBox>
#include <QProcess>
#include <QDir>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHeaderView>
#include <QTimer>

PlatformPage::PlatformPage(QWidget *parent) : QWidget(parent)
{
    setupUi();
}

void PlatformPage::setDatabase(Database *db)
{
    m_db = db;

    // 从 SQLite 加载 platform_urls 到 m_configJson
    QString val = m_db->loadConfig("platform_urls",
        R"({"deepseek":"https://chat.deepseek.com/"})");
    QJsonDocument doc = QJsonDocument::fromJson(val.toUtf8());
    m_configJson["platform_urls"] = doc.isObject() ? doc.object() : QJsonObject{};

    // 载入平台 URL 到表格
    QJsonObject urls = m_configJson["platform_urls"].toObject();
    QStringList keys = urls.keys();
    m_platformTable->setRowCount(keys.size());
    for (int i = 0; i < keys.size(); i++) {
        m_platformTable->setItem(i, 0, new QTableWidgetItem(keys[i]));
        m_platformTable->setItem(i, 1, new QTableWidgetItem(urls[keys[i]].toString()));
    }
    refreshTypingDropdown();
}

void PlatformPage::setBrowserManager(BrowserManager *bm)
{
    m_browser = bm;
}

// ============================================================
// UI 布局
// ============================================================

void PlatformPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);

    // 操作按钮行
    auto *btnRow = new QHBoxLayout();
    auto *addPlatBtn = new QPushButton("+ 新增平台", this);
    addPlatBtn->setProperty("cssClass", "success");
    connect(addPlatBtn, &QPushButton::clicked, this, [this]() {
        int row = m_platformTable->rowCount();
        m_platformTable->insertRow(row);
        m_platformTable->setItem(row, 0, new QTableWidgetItem("新平台"));
        m_platformTable->setItem(row, 1, new QTableWidgetItem("https://"));
        refreshTypingDropdown();
    });
    btnRow->addWidget(addPlatBtn);

    auto *delPlatBtn = new QPushButton("− 删除选中", this);
    delPlatBtn->setProperty("cssClass", "danger");
    connect(delPlatBtn, &QPushButton::clicked, this, [this]() {
        int row = m_platformTable->currentRow();
        if (row >= 0) {
            QString name = m_platformTable->item(row, 0)
                ? m_platformTable->item(row, 0)->text() : "?";
            auto answer = QMessageBox::question(this, "确认删除",
                QString("确定删除平台「%1」吗？\n此操作不可恢复。").arg(name));
            if (answer == QMessageBox::Yes) {
                m_platformTable->removeRow(row);
                onSavePlatforms();
            }
        } else {
            Toast::show("请先选中要删除的平台行", Toast::Info, this);
        }
    });
    btnRow->addWidget(delPlatBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    // 平台表格
    m_platformTable = new QTableWidget(0, 2, this);
    m_platformTable->setHorizontalHeaderLabels({"平台名称", "会话链接 (聊天页URL)"});
    m_platformTable->horizontalHeader()->setStretchLastSection(true);
    m_platformTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_platformTable, 1);

    // 帮助说明
    auto *helpLabel = new QLabel(
        "<b>如何获取会话链接？</b><br>"
        "1. 在 Edge/Chrome 浏览器中打开 AI 平台<br>"
        "2. 登录并开始一个<u>新聊天</u><br>"
        "3. 复制地址栏完整 URL<br>"
        "&nbsp;&nbsp;&nbsp;例: <code>https://chat.deepseek.com/a/chat/s/xxxxx</code><br>"
        "4. 粘贴到上方表格「会话链接」列<br>"
        "<br>"
        "<span style='color:#ff6b6b;'>⚠ 不要粘贴首页 URL（如 https://chat.deepseek.com/），<br>"
        "&nbsp;&nbsp;&nbsp;首页无法搜索，会导致搜索返回空结果。</span>",
        this);
    helpLabel->setStyleSheet(QString("color:%1; font-size:11px; padding:6px 8px;"
        "background:%2; border-radius:4px; line-height:1.5;")
        .arg(Theme::TextSecondary, Theme::BgSecondary));
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    auto *saveBtn = new QPushButton("保存平台配置", this);
    saveBtn->setProperty("cssClass", "primary");
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        onSavePlatforms();
        refreshTypingDropdown();
    });
    layout->addWidget(saveBtn);

    // 平台定型区
    auto *typingGroup = new QGroupBox("平台定型", this);
    auto *typingForm = new QFormLayout(typingGroup);
    m_typingPlatform = new QComboBox(this);
    typingForm->addRow("目标平台:", m_typingPlatform);

    m_typingText = new QCheckBox("文字收发定型", this);
    m_typingText->setChecked(true);
    typingForm->addRow("", m_typingText);
    m_typingFile = new QCheckBox("文件上传定型", this);
    typingForm->addRow("", m_typingFile);

    m_typingBtn = new QPushButton("开始定型", this);
    m_typingBtn->setProperty("cssClass", "primary");
    connect(m_typingBtn, &QPushButton::clicked, this, &PlatformPage::onStartTyping);
    typingForm->addRow("", m_typingBtn);

    m_typingStatus = new QLabel("未定型", this);
    m_typingStatus->setStyleSheet(QString("color:%1; font-size:11px; padding:4px 0;").arg(Theme::TextMuted));
    typingForm->addRow("状态:", m_typingStatus);
    layout->addWidget(typingGroup);

    // 状态标签
    m_envStatus = new QLabel("", this);
    m_envStatus->setStyleSheet(QString("color:%1; font-size:11px; padding:4px 0;").arg(Theme::TextMuted));
    m_envStatus->setWordWrap(true);
    layout->addWidget(m_envStatus);
}

// ============================================================
// 平台操作
// ============================================================

void PlatformPage::onSavePlatforms()
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
    if (m_db) {
        QJsonDocument doc(urls);
        m_db->saveConfig("platform_urls", QString::fromUtf8(doc.toJson()));
    }
    Toast::show("平台配置已保存 ✓", Toast::Success, this);
    m_envStatus->setText(QString("<span style='color:%1;'>✓ 平台配置已保存 (%2 个平台)</span>")
                             .arg(Theme::Green).arg(urls.size()));
}

// ============================================================
// 平台定型
// ============================================================

void PlatformPage::onStartTyping()
{
    QString plat = m_typingPlatform->currentText();
    QStringList targets;
    if (m_typingText->isChecked()) targets.append("文字收发");
    if (m_typingFile->isChecked()) targets.append("文件上传");
    if (targets.isEmpty()) {
        Toast::show("请至少选择一个定型目标", Toast::Warning, this);
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
    int cdpPort = m_db ? m_db->loadConfig("cdp_port", "9223").toInt() : 9223;
    if (cdpPort < 1) cdpPort = 9223;

    // 检查 API Key
    QString apiHash = m_db ? m_db->loadConfig("deepseek_key_hash") : QString();
    if (apiHash.isEmpty()) {
        m_envStatus->setText(QString("<span style='color:%1;'>✗ 定型需要 DeepSeek API Key，请先配置</span>")
                                 .arg(Theme::Error));
        Toast::show("定型需要 DeepSeek API Key，请先在配置页配置", Toast::Warning, this);
        return;
    }

    // 检查浏览器
    if (!m_browser || !m_browser->isCdpAvailable(cdpPort)) {
        m_envStatus->setText(QString("<span style='color:%1;'>✗ 浏览器未就绪 (端口 %2)，请先启动</span>")
                                 .arg(Theme::Error).arg(cdpPort));
        Toast::show(QString("浏览器未就绪 (端口 %1)，请先启动浏览器").arg(cdpPort), Toast::Warning, this);
        return;
    }

    // 确认对话框
    auto answer = QMessageBox::question(this, "定型",
        QString("将对「%1」平台进行定型\nURL: %2\nCDP: %3\n定型目标: %4\n\n"
                "定型过程约需 60 秒，请保持浏览器窗口打开。\n继续？")
            .arg(plat, platUrl).arg(cdpPort).arg(targets.join(" + ")));
    if (answer != QMessageBox::Yes) {
        m_envStatus->setText(QString("<span style='color:%1;'>已取消定型</span>").arg(Theme::TextMuted));
        return;
    }

    // 解密 API Key
    QString apiKey;
    if (m_db) {
        QString encrypted = m_db->loadConfig("deepseek_key", "");
        if (!encrypted.isEmpty()) {
            apiKey = Crypto::decrypt(encrypted);
        }
    }
    QString apiUrl = m_db ? m_db->loadConfig("deepseek_api", "https://api.deepseek.com/v1") : "https://api.deepseek.com/v1";

    // 获取 Python 路径
    QString pythonPath = m_db ? m_db->loadConfig("python_path", "python") : "python";

    // 启动 typing_agent.py
    QProcess *proc = new QProcess(this);
    QString agentDir;
    QDir agentBase(QCoreApplication::applicationDirPath());
    for (int up = 0; up <= 2; up++) {
        QString candidate = QDir::cleanPath(agentBase.absolutePath() + "/agent");
        if (QFile::exists(candidate + "/typing_agent.py")) {
            agentDir = candidate;
            break;
        }
        if (!agentBase.cdUp()) break;
    }
    if (agentDir.isEmpty()) {
        agentDir = QDir::currentPath() + "/agent";
    }
    QString typingPy = agentDir + "/typing_agent.py";

    QStringList args;
    args << typingPy << plat << platUrl
         << "--cdp-port" << QString::number(cdpPort)
         << "--api-key" << apiKey
         << "--api-url" << apiUrl;
    if (!m_typingText->isChecked())
        args << "--skip-text";
    if (m_typingFile->isChecked())
        args << "--file-upload";

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUTF8", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    proc->setProcessEnvironment(env);
    proc->setWorkingDirectory(agentDir);
    proc->setProgram(pythonPath);

    auto resetTypingUI = [this]() {
        m_typingBtn->setEnabled(true);
        m_typingBtn->setText("开始定型");
    };

    m_typingBtn->setEnabled(false);
    m_typingBtn->setText("定型中...");
    m_typingStatus->setText(QString("● 正在定型 %1 ...").arg(plat));
    m_typingStatus->setStyleSheet(QString("color:%1; font-size:11px; padding:4px 0;").arg(Theme::Info));
    m_envStatus->setText(QString("<span style='color:%1;'>● 定型中：%2 ...</span>")
                             .arg(Theme::Info).arg(plat));

    connect(proc, &QProcess::errorOccurred, this, [this, plat, resetTypingUI](QProcess::ProcessError err) {
        resetTypingUI();
        QString msg;
        switch (err) {
            case QProcess::FailedToStart: msg = "Python 未找到，请先运行环境检测"; break;
            case QProcess::Timedout:      msg = "定型进程超时"; break;
            default:                      msg = QString("进程错误 (code=%1)").arg(static_cast<int>(err)); break;
        }
        m_typingStatus->setText(QString("✗ %1 定型失败: %2").arg(plat, msg));
        m_typingStatus->setStyleSheet(QString("color:%1; font-size:11px; padding:4px 0;").arg(Theme::Error));
        m_envStatus->setText(QString("<span style='color:%1;'>✗ 定型失败: %2</span>")
                                 .arg(Theme::Error).arg(msg));
        Toast::show(QString("定型失败: %1").arg(msg), Toast::Error, this);
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, plat, proc, resetTypingUI](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        resetTypingUI();

        if (exitCode == 0) {
            m_typingStatus->setText(QString("✓ %1 定型成功").arg(plat));
            m_typingStatus->setStyleSheet(
                QString("color:%1; font-size:11px; font-weight:bold; padding:4px 0;").arg(Theme::Green));
            m_envStatus->setText(QString("<span style='color:%1;'>✓ %1 定型成功</span>")
                                     .arg(Theme::Green).arg(plat));
            Toast::show(QString("%1 定型成功 ✓").arg(plat), Toast::Success, this);
            refreshTypingDropdown();
        } else {
            QString errOutput = QString::fromUtf8(proc->readAllStandardError());
            QString detail = errOutput.isEmpty()
                ? QString("退出码 %1").arg(exitCode)
                : errOutput.left(200);
            m_typingStatus->setText(QString("✗ %1 定型失败: %2").arg(plat, detail));
            m_typingStatus->setStyleSheet(
                QString("color:%1; font-size:11px; padding:4px 0;").arg(Theme::Error));
            m_envStatus->setText(QString("<span style='color:%1;'>✗ %1 定型失败: %2</span>")
                                     .arg(Theme::Error).arg(plat, detail.left(100)));
            Toast::show(QString("%1 定型失败").arg(plat), Toast::Error, this);
        }
    });

    connect(proc, &QProcess::readyReadStandardOutput, this, [proc]() {
        qInfo().noquote() << "[Typing]" << proc->readAllStandardOutput().trimmed();
    });
    connect(proc, &QProcess::readyReadStandardError, this, [proc]() {
        qWarning().noquote() << "[Typing]" << proc->readAllStandardError().trimmed();
    });

    proc->setArguments(args);
    proc->start();
}

// ============================================================
// 下拉框刷新
// ============================================================

void PlatformPage::refreshTypingDropdown()
{
    if (!m_typingPlatform) return;

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
    static const QStringList builtin = {"deepseek"};
    for (const auto &p : builtin)
        known.insert(p);

    QString current = m_typingPlatform->currentText();
    m_typingPlatform->clear();
    for (const auto &p : known)
        m_typingPlatform->addItem(p);

    int idx = m_typingPlatform->findText(current);
    if (idx >= 0) m_typingPlatform->setCurrentIndex(idx);
}

void PlatformPage::refreshSearchPlatformDropdowns()
{
    // 由 ConfigPage 调用，此处不再需要
}
