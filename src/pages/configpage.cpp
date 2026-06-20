#include "configpage.h"
#include "../core/database.h"
#include "../utils/crypto.h"
#include "../utils/logger.h"
#include "../core/browsermanager.h"
#include "../ui/theme.h"
#include "../ui/toast.h"
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
#include <QTimer>

ConfigPage::ConfigPage(QWidget *parent) : QWidget(parent)
{
    setupUi();
}

void ConfigPage::setDatabase(Database *db)
{
    m_db = db;
    loadConfigFromSQLite();
    refreshSearchPlatformDropdowns();
    onLoadConfig();
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
        Toast::show("请输入有效的端口号 (1-65535)", Toast::Warning, this);
        return;
    }
    if (m_browser->isRunning()) {
        Toast::show("浏览器已在运行中", Toast::Info, this);
        return;
    }
    if (m_db) m_db->saveConfig("cdp_port", QString::number(port));
    m_envStatus->setText(QString("<span style='color:%1;'>● 正在启动浏览器 (端口 %2)...</span>").arg(Theme::Info).arg(port));
    QCoreApplication::processEvents();
    int result = m_browser->launch(port);
    if (result > 0) {
        m_envStatus->setText(QString("<span style='color:%1;'>✓ 浏览器已就绪 (端口 %2)</span>").arg(Theme::Green).arg(result));
        Toast::show(QString("浏览器已在端口 %1 启动").arg(result), Toast::Success, this);
    } else {
        m_envStatus->setText(QString("<span style='color:%1;'>✗ 浏览器启动失败 (端口 %2)</span>").arg(Theme::Error).arg(port));
        Toast::show(QString("浏览器 CDP 端口 %1 未就绪").arg(port), Toast::Error, this);
    }
}

// ============================================================
// 配置加载（SQLite 为唯一权威源）
// ============================================================

bool ConfigPage::loadConfigFromSQLite()
{
    if (!m_db) return false;

    m_db->seedDefaultConfig();

    auto loadJson = [&](const QString &key, const QString &def) -> QJsonObject {
        QString val = m_db->loadConfig(key, def);
        QJsonDocument doc = QJsonDocument::fromJson(val.toUtf8());
        return doc.isObject() ? doc.object() : QJsonObject{};
    };

    m_configJson = QJsonObject{};
    m_configJson["platform_urls"] = loadJson("platform_urls",
        R"({"deepseek":"https://chat.deepseek.com/"})");
    m_configJson["chat_path_patterns"] = loadJson("chat_path_patterns",
        R"({"deepseek":["/a/chat/s/"]})");
    m_configJson["cdp_port"] = m_db->loadConfig("cdp_port", "9223").toInt();
    m_configJson["search_platform"] = m_db->loadConfig("search_platform", "deepseek");
    m_configJson["synthesis_platform"] = m_db->loadConfig("synthesis_platform", "deepseek");
    m_configJson["deepseek_api"] = m_db->loadConfig("deepseek_api", "https://api.deepseek.com/v1");

    return true;
}

// ============================================================
// UI 布局
// ============================================================

void ConfigPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);

    auto *form = new QFormLayout();
    form->setSpacing(16);

    auto *cdpRow = new QHBoxLayout();
    m_cdpPort = new QLineEdit("9223", this);
    cdpRow->addWidget(m_cdpPort, 1);
    auto *launchBtn = new QPushButton("启动浏览器", this);
    connect(launchBtn, &QPushButton::clicked, this, &ConfigPage::onLaunchBrowser);
    cdpRow->addWidget(launchBtn);
    form->addRow("CDP 端口:", cdpRow);

    m_apiKey = new QLineEdit(this);
    m_apiKey->setEchoMode(QLineEdit::Password);
    form->addRow("DeepSeek Key:", m_apiKey);

    m_apiKeyStatus = new QLabel("未配置", this);
    m_apiKeyStatus->setStyleSheet(QString("color:%1;").arg(Theme::TextMuted));
    form->addRow("状态:", m_apiKeyStatus);

    m_apiUrl = new QLineEdit("https://api.deepseek.com/v1", this);
    form->addRow("API 端点:", m_apiUrl);

    m_defaultSearchPlatform = new QComboBox(this);
    form->addRow("默认搜索平台:", m_defaultSearchPlatform);

    m_defaultSynthesisPlatform = new QComboBox(this);
    form->addRow("默认整合平台:", m_defaultSynthesisPlatform);

    auto *saveBtn = new QPushButton("保存配置", this);
    saveBtn->setProperty("cssClass", "primary");
    connect(saveBtn, &QPushButton::clicked, this, &ConfigPage::onSaveConfig);
    form->addRow("", saveBtn);

    // 自动分析深度
    m_autoDepth = new QCheckBox("自动分析问题等级 (开启后系统自动判断 L2/L3)", this);
    m_autoDepth->setChecked(true);
    form->addRow("", m_autoDepth);

    // 自动启动浏览器
    m_autoLaunchBrowser = new QCheckBox("启动程序时自动启动浏览器", this);
    form->addRow("", m_autoLaunchBrowser);

    // 环境自举
    auto *envBtn = new QPushButton("环境检测与修复", this);
    connect(envBtn, &QPushButton::clicked, this, &ConfigPage::onDetectEnvironment);
    form->addRow("一键自举:", envBtn);

    m_envStatus = new QLabel("点击「环境检测与修复」一键检查并自动修复环境问题", this);
    m_envStatus->setStyleSheet(QString("color:%1; font-size:11px; padding:4px 0;").arg(Theme::TextMuted));
    m_envStatus->setWordWrap(true);
    form->addRow("", m_envStatus);

    // 日志级别
    m_logLevelCombo = new QComboBox(this);
    m_logLevelCombo->addItems({"ERROR", "WARNING", "INFO", "DEBUG"});
    m_logLevelCombo->setCurrentIndex(2);
    connect(m_logLevelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConfigPage::onLogLevelChanged);
    form->addRow("日志级别:", m_logLevelCombo);

    layout->addLayout(form);
    layout->addStretch();
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

    // 首次保存时写入 chat_path_patterns 默认值
    if (m_db->loadConfig("chat_path_patterns", "").isEmpty()) {
        QJsonObject patterns;
        { QJsonArray a; a.append(QString("/a/chat/s/")); patterns["deepseek"] = a; }
        QJsonDocument pdoc(patterns);
        m_db->saveConfig("chat_path_patterns", QString::fromUtf8(pdoc.toJson()));
    }

    // 首次保存时写入 platform_urls 默认值
    if (m_db->loadConfig("platform_urls", "").isEmpty()) {
        QJsonObject urls;
        urls["deepseek"] = "https://chat.deepseek.com/";
        QJsonDocument udoc(urls);
        m_db->saveConfig("platform_urls", QString::fromUtf8(udoc.toJson()));
    }

    QString apiKey = m_apiKey->text();
    if (!apiKey.isEmpty()) {
        QString encrypted = Crypto::encrypt(apiKey);
        if (!encrypted.isEmpty()) {
            m_db->saveConfig("deepseek_key", encrypted);
            QString hash = Crypto::sha256Prefix8(apiKey);
            m_db->saveConfig("deepseek_key_hash", hash);
            m_apiKeyStatus->setText(QString("已加密存储 (Hash: %1) ✓").arg(hash));
            m_apiKeyStatus->setStyleSheet(QString("color:%1;").arg(Theme::Success));
            m_apiKey->clear();
            m_apiKey->setPlaceholderText(QString("已加密存储 — 输入新 Key 可替换 (Hash: %1)").arg(hash));
        } else {
            m_apiKeyStatus->setText("加密失败，请重试");
            m_apiKeyStatus->setStyleSheet(QString("color:%1; font-weight:bold;").arg(Theme::Error));
            Toast::show("API Key 加密失败，请重试", Toast::Error, this);
            return;
        }
    }

    m_apiKeyStatus->setStyleSheet(QString("color:%1; font-weight:bold;").arg(Theme::Success));
    m_apiKeyStatus->setText("配置已保存 ✓");
    m_envStatus->setText(QString("<span style='color:%1;'>✓ 配置已保存</span>").arg(Theme::Green));
    Toast::show("配置已保存 ✓", Toast::Success, this);
    QTimer::singleShot(3000, this, [this]() {
        QString hash = m_db ? m_db->loadConfig("deepseek_key_hash") : QString();
        if (hash.isEmpty()) {
            m_apiKeyStatus->setText("未配置");
            m_apiKeyStatus->setStyleSheet(QString("color:%1;").arg(Theme::TextMuted));
            m_apiKey->setPlaceholderText("请输入 DeepSeek API Key");
        } else {
            m_apiKeyStatus->setText(QString("已配置 (Hash: %1) ✓").arg(hash));
            m_apiKeyStatus->setStyleSheet(QString("color:%1; font-weight:bold;").arg(Theme::Success));
            m_apiKey->setPlaceholderText(QString("已加密存储 — 输入新 Key 可替换 (Hash: %1)").arg(hash));
        }
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

    QString synthPlat = m_db->loadConfig("synthesis_platform", "deepseek");
    int kpIdx = m_defaultSynthesisPlatform->findText(synthPlat);
    if (kpIdx >= 0) m_defaultSynthesisPlatform->setCurrentIndex(kpIdx);

    bool autoLaunch = m_db->loadConfig("auto_launch_browser", "0") == "1";
    bool autoDepth = m_db->loadConfig("auto_depth", "1") == "1";
    m_autoDepth->setChecked(autoDepth);
    m_autoLaunchBrowser->setChecked(autoLaunch);

    QString hash = m_db->loadConfig("deepseek_key_hash");
    if (hash.isEmpty()) {
        m_apiKeyStatus->setText("未配置");
        m_apiKeyStatus->setStyleSheet(QString("color:%1;").arg(Theme::TextMuted));
        m_apiKey->setPlaceholderText("请输入 DeepSeek API Key");
    } else {
        m_apiKeyStatus->setText(QString("已配置 (Hash: %1) ✓").arg(hash));
        m_apiKeyStatus->setStyleSheet(QString("color:%1; font-weight:bold;").arg(Theme::Success));
        m_apiKey->setPlaceholderText(QString("已加密存储 — 输入新 Key 可替换 (Hash: %1)").arg(hash));
    }
}

// ============================================================
// 环境自举
// ============================================================

void ConfigPage::onDetectEnvironment()
{
    if (!m_db) return;

    auto setStatus = [this](const QString &msg) {
        m_envStatus->setStyleSheet(QString("color:%1; font-size:11px; padding:4px 0;").arg(Theme::Info));
        m_envStatus->setText(msg);
        QCoreApplication::processEvents();
    };

    setStatus("● 检测中...");

    QStringList okLines;
    QStringList warnLines;
    QStringList errLines;

    // ============================================================
    // 1. 检测 Python
    // ============================================================
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
    if (pythonPath.isEmpty()) {
        errLines.append("✗ Python: 未找到 (请安装 Python 3.12+)");
    }

    // ============================================================
    // 2. 检测 Playwright → 缺失则自动安装
    // ============================================================
    if (!pythonPath.isEmpty()) {
        bool hasPlaywright = false;
        {
            QProcess pwProc;
            pwProc.start(pythonPath, {"-c", "import playwright; print(playwright.__file__)"});
            hasPlaywright = pwProc.waitForFinished(5000) && pwProc.exitCode() == 0;
        }

        if (hasPlaywright) {
            okLines.append("✓ Playwright: 已安装");
        } else {
            warnLines.append("△ Playwright: 未安装 → 正在自动安装...");
            setStatus(QString("● 正在安装 Playwright...\n%1").arg(
                okLines.join("\n") + "\n" + warnLines.join("\n")));

            // pip install playwright
            {
                QProcess pip;
                pip.start(pythonPath, {"-m", "pip", "install", "playwright"});
                if (pip.waitForFinished(60000) && pip.exitCode() == 0) {
                    warnLines.last() = "△ Playwright: pip 安装完成 → 安装浏览器...";
                    setStatus(QString("● 正在安装 Playwright 浏览器...\n%1").arg(
                        okLines.join("\n") + "\n" + warnLines.join("\n")));

                    // playwright install chromium
                    QProcess install;
                    install.start(pythonPath, {"-m", "playwright", "install", "chromium"});
                    if (install.waitForFinished(120000) && install.exitCode() == 0) {
                        warnLines.removeLast();
                        okLines.append("✓ Playwright: 已自动安装完成");
                    } else {
                        warnLines.last() = "△ Playwright: 浏览器安装失败 (请手动运行 playwright install chromium)";
                    }
                } else {
                    warnLines.last() = "△ Playwright: pip 安装失败 (请手动运行 pip install playwright)";
                }
            }
        }
    }

    // ============================================================
    // 3. 检测浏览器
    // ============================================================
    QString browserPath = BrowserManager::findBrowserPath(BrowserManager::Edge);
    if (browserPath.isEmpty())
        browserPath = BrowserManager::findBrowserPath(BrowserManager::Chrome);
    if (!browserPath.isEmpty())
        okLines.append(QString("✓ 浏览器: %1").arg(QFileInfo(browserPath).baseName()));
    else
        warnLines.append("△ 浏览器: 未找到 Edge/Chrome");

    // ============================================================
    // 4. 扫描 CDP 端口 → 无响应则自动启动
    // ============================================================
    if (m_browser) {
        int port = m_browser->scanCdpPort(9223, 9226);
        if (port > 0) {
            okLines.append(QString("✓ CDP 端口: %1").arg(port));
            m_cdpPort->setText(QString::number(port));
        } else if (!browserPath.isEmpty()) {
            warnLines.append(QString("△ CDP: 端口无响应 → 正在自动启动浏览器...").arg(9223));
            setStatus(QString("● 正在启动浏览器...\n%1").arg(
                okLines.join("\n") + "\n" + warnLines.join("\n")));

            m_cdpPort->setText("9223");
            m_db->saveConfig("cdp_port", "9223");
            int result = m_browser->launch(9223);
            if (result > 0) {
                warnLines.removeLast();
                okLines.append(QString("✓ 浏览器: 已自动启动 (CDP 端口 %1)").arg(result));
            } else {
                warnLines.last() = QString("△ CDP: 浏览器启动失败 (请手动启动并指定 --remote-debugging-port=%1)").arg(9223);
            }
        } else {
            warnLines.append(QString("△ CDP: 端口 %1 无响应 (未找到浏览器)").arg(9223));
        }
    }

    // ============================================================
    // 5. 检查 API Key
    // ============================================================
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

    // ============================================================
    // 6. 平台 URL 完整性
    // ============================================================
    QJsonObject urls = m_configJson["platform_urls"].toObject();
    if (urls.isEmpty())
        warnLines.append("△ 平台 URL: 未注册任何平台");
    else
        okLines.append(QString("✓ 平台 URL: 已注册 %1 个平台").arg(urls.size()));

    // ============================================================
    // 汇总显示
    // ============================================================
    QStringList allLines;
    allLines << okLines << warnLines << errLines;
    QString html = allLines.join("<br>");
    html.replace("✓", QString("<span style='color:%1;'>✓</span>").arg(Theme::Green));
    html.replace("✗", QString("<span style='color:%1;'>✗</span>").arg(Theme::Error));
    html.replace("△", QString("<span style='color:%1;'>△</span>").arg(Theme::Warning));

    bool allOk = errLines.isEmpty() && warnLines.isEmpty();
    QString overall = allOk
        ? QString("<span style='color:%1;'>● 环境就绪</span>").arg(Theme::Green)
        : (errLines.isEmpty()
           ? QString("<span style='color:%1;'>● 环境基本可用 (部分警告)</span>").arg(Theme::Warning)
           : QString("<span style='color:%1;'>● 环境不可用 (需手动修复)</span>").arg(Theme::Error));
    m_envStatus->setText(overall + "<br>" + html);

    Toast::show(allOk ? "环境就绪" : (errLines.isEmpty() ? "环境基本可用 (有警告)" : "环境存在问题"),
                allOk ? Toast::Success : (errLines.isEmpty() ? Toast::Warning : Toast::Error), this);
}

// ============================================================
// 日志级别 + 平台下拉框
// ============================================================

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

void ConfigPage::refreshSearchPlatformDropdowns()
{
    if (!m_defaultSearchPlatform) return;

    QJsonObject urls = m_configJson["platform_urls"].toObject();
    QStringList platforms = urls.keys();
    if (platforms.isEmpty())
        platforms = {"deepseek"};

    QString curSearch = m_defaultSearchPlatform->currentText();
    QString curSynth = m_defaultSynthesisPlatform->currentText();

    m_defaultSearchPlatform->clear();
    m_defaultSynthesisPlatform->clear();
    m_defaultSearchPlatform->addItems(platforms);
    m_defaultSynthesisPlatform->addItems(platforms);

    int si = m_defaultSearchPlatform->findText(curSearch);
    if (si >= 0) m_defaultSearchPlatform->setCurrentIndex(si);
    int ki = m_defaultSynthesisPlatform->findText(curSynth);
    if (ki >= 0) m_defaultSynthesisPlatform->setCurrentIndex(ki);
}
