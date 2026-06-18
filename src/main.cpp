/**
 * Certus C++ 后端 —— 命令行入口。
 *
 * 面向两类用户：
 *   1. 人类终端：certus_backend search "问题"           （人类可读输出）
 *   2. 外部 Agent：certus_backend search --json --stdin （JSON 行输出）
 *
 * 命令列表：
 *   certus_backend search "query" [--depth L3] [--json] [--stdin] [-o PATH]
 *   certus_backend browser --start [--port 9223]
 *   certus_backend browser --stop
 *   certus_backend config --cdp-port 9223 [--api-key KEY]
 *   certus_backend status [--json]
 */

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "core/agentmanager.h"
#include "core/browsermanager.h"
#include "core/database.h"
#include "utils/crypto.h"

// ============================================================
// JSON 行输出辅助
// ============================================================

static bool g_jsonMode = false;
static QTextStream g_stdout(stdout);

static void emitJson(const QString &event, const QJsonObject &fields = {})
{
    if (!g_jsonMode) return;
    QJsonObject obj;
    obj["event"] = event;
    for (auto it = fields.begin(); it != fields.end(); ++it)
        obj[it.key()] = it.value();
    g_stdout << QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)) << "\n";
    g_stdout.flush();
}

static void emitHuman(const QString &line)
{
    if (g_jsonMode) return;
    g_stdout << line << "\n";
    g_stdout.flush();
}

static void emitError(const QString &errorType, const QString &detail)
{
    if (g_jsonMode) {
        QJsonObject fields;
        fields["error_type"] = errorType;
        fields["detail"] = detail;
        emitJson("error", fields);
    } else {
        g_stdout << "✗ " << errorType << ": " << detail << "\n";
        g_stdout.flush();
    }
}

// ============================================================
// 搜索命令
// ============================================================

int cmdSearch(const QString &query, const QString &depth,
              const QString &searchPlatform, const QString &synthesisPlatform,
              const QString &cdpPort, bool mockCdp, const QString &outputPath)
{
    QString dbPath = QDir::currentPath() + "/data/certus.db";
    Database db(dbPath);
    if (!db.open()) {
        emitError("db_open_failed", "无法打开数据库: " + dbPath);
        return 1;
    }

    Database::SessionStatus status = db.getSessionStatus();
    if (status == Database::Running) {
        db.setSessionStatus(Database::Idle);
    }

    AgentManager agent;
    agent.setDatabase(&db);

    QString agentDir = QCoreApplication::applicationDirPath() + "/../agent";
    if (!QDir(agentDir).exists()) {
        agentDir = QDir::currentPath() + "/agent";
    }
    agent.setAgentDir(QDir::cleanPath(agentDir));
    agent.setPythonPath("python");

    // 输出启动信息
    emitJson("search_started", {
        {"query", query},
        {"depth", depth},
        {"search_platform", searchPlatform},
        {"synthesis_platform", synthesisPlatform},
    });

    // === 信号 → 结构化输出 ===

    QObject::connect(&agent, &AgentManager::stageStarted,
        [](const QString &stage, const QString &question, const QString &platform) {
            emitJson("stage_start", {
                {"stage", stage},
                {"question", question.left(100)},
                {"platform", platform},
            });
            emitHuman(QString("[%1] %2 | %3").arg(stage, question.left(50), platform));
        });

    QObject::connect(&agent, &AgentManager::stageFinished,
        [](const QString &stage, const QString &platform, int contentLen) {
            emitJson("stage_done", {
                {"stage", stage},
                {"platform", platform},
                {"content_len", contentLen},
            });
            emitHuman(QString("  ✓ %1 | %2 字符").arg(stage).arg(contentLen));
        });

    QObject::connect(&agent, &AgentManager::stageProgress,
        [](const QString &stage, const QString &platform, int elapsedSec, int contentLen) {
            emitJson("stage_progress", {
                {"stage", stage},
                {"platform", platform},
                {"elapsed_sec", elapsedSec},
                {"content_len", contentLen},
            });
        });

    QObject::connect(&agent, &AgentManager::errorOccurred,
        [](const QString &errorType, const QString &platform, const QString &detail) {
            emitJson("agent_error", {
                {"error_type", errorType},
                {"platform", platform},
                {"detail", detail},
            });
            emitHuman(QString("✗ 错误 [%1] %2: %3").arg(platform, errorType, detail));
        });

    QObject::connect(&agent, &AgentManager::searchFinished,
        [&outputPath](bool success, const QString &reportPath) {
            emitJson("search_finished", {
                {"success", success},
                {"report_path", reportPath},
            });
            emitHuman(QString("搜索完成 | 报告: %1").arg(reportPath));

            // -o 指定输出路径：复制报告内容到指定文件
            if (!outputPath.isEmpty() && !reportPath.isEmpty()) {
                QFile src(reportPath);
                if (src.open(QIODevice::ReadOnly)) {
                    QFile dst(outputPath);
                    if (dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        dst.write(src.readAll());
                        emitJson("report_saved", {{"path", outputPath}});
                    }
                }
            }
            QCoreApplication::quit();
        });

    QObject::connect(&agent, &AgentManager::searchCancelled,
        [](const QString &partialResult) {
            emitJson("search_cancelled", {
                {"partial_result_len", partialResult.length()},
            });
            emitHuman("搜索已取消");
            QCoreApplication::quit();
        });

    // 启动
    if (!agent.start(query, depth, searchPlatform, synthesisPlatform,
                     cdpPort, mockCdp)) {
        emitError("start_failed", "搜索启动失败（配置不完整或已有搜索在运行）");
        return 1;
    }

    return QCoreApplication::exec();
}

// ============================================================
// 浏览器命令
// ============================================================

int cmdBrowser(bool start, int port)
{
    BrowserManager bm;

    if (start) {
        int cdpPort = bm.launch(port);
        if (cdpPort > 0) {
            emitJson("browser_launched", {{"cdp_port", cdpPort}});
            emitHuman(QString("浏览器已启动 | CDP 端口: %1").arg(cdpPort));
            return 0;
        }
        emitError("browser_launch_failed",
                  QString("端口 %1 启动失败（浏览器路径不存在或端口被占用）").arg(port));
        return 1;
    }

    // --stop
    if (port > 0) {
        bm.shutdown(port);
    } else {
        bm.shutdownAll();
    }
    emitJson("browser_closed", {{"port", port}});
    emitHuman("浏览器已关闭");
    return 0;
}

// ============================================================
// 配置命令
// ============================================================

int cmdConfig(const QString &cdpPort, const QString &apiKey)
{
    QString dbPath = QDir::currentPath() + "/data/certus.db";
    Database db(dbPath);
    if (!db.open()) {
        emitError("db_open_failed", "无法打开数据库");
        return 1;
    }

    if (!cdpPort.isEmpty()) {
        db.saveConfig("cdp_port", cdpPort);
        emitJson("config_set", {{"key", "cdp_port"}, {"value", cdpPort}});
        emitHuman("CDP 端口已设为: " + cdpPort);
    }

    if (!apiKey.isEmpty()) {
        QString encrypted = Crypto::encrypt(apiKey);
        if (encrypted.isEmpty()) {
            emitError("encrypt_failed", "API Key 加密失败");
            return 1;
        }
        db.saveConfig("deepseek_key", encrypted);
        db.saveConfig("deepseek_key_hash", Crypto::sha256Prefix8(apiKey));
        emitJson("config_set", {{"key", "api_key"}, {"value", "***"}});
        emitHuman("API Key 已加密存储");
    }

    return 0;
}

// ============================================================
// 状态命令
// ============================================================

int cmdStatus()
{
    QString dbPath = QDir::currentPath() + "/data/certus.db";
    Database db(dbPath);
    if (!db.open()) {
        emitJson("status", {
            {"db", "not_initialized"},
            {"session", "idle"},
        });
        emitHuman("数据库未初始化");
        return 0;
    }

    Database::SessionStatus status = db.getSessionStatus();
    QString statusName;
    switch (status) {
        case Database::Idle:     statusName = "idle";     break;
        case Database::Running:  statusName = "running";  break;
        case Database::Evolving: statusName = "evolving"; break;
    }

    // 配置
    QString cdpPort = db.loadConfig("cdp_port");
    QString apiKeyHash = db.loadConfig("deepseek_key_hash");

    emitJson("status", {
        {"session", statusName},
        {"cdp_port", cdpPort.isEmpty() ? "unset" : cdpPort},
        {"api_key_configured", !apiKeyHash.isEmpty()},
    });

    if (!g_jsonMode) {
        emitHuman(QString("会话状态: %1").arg(statusName));

        auto searches = db.recentSearches(5);
        emitHuman("\n最近搜索:");
        for (const auto &s : searches) {
            emitHuman(QString("  [%1] %2 | %3 | %4")
                          .arg(s.startedAt, s.query.left(40), s.platform, s.status));
        }

        emitHuman("");
        emitHuman(QString("CDP 端口: ") + (cdpPort.isEmpty() ? QString("未设置") : cdpPort));
        emitHuman(QString("API Key: ") + (apiKeyHash.isEmpty() ? QString("未设置") : QString("已配置")));
    }

    return 0;
}

// ============================================================
// 配置验证命令
// ============================================================

int cmdValidate()
{
    QString dbPath = QDir::currentPath() + "/data/certus.db";
    Database db(dbPath);
    if (!db.open()) {
        emitError("db_open_failed", "无法打开数据库");
        return 1;
    }

    AgentManager agent;
    agent.setDatabase(&db);

    // Agent 路径探测
    QString agentDir = QCoreApplication::applicationDirPath() + "/../agent";
    if (!QDir(agentDir).exists())
        agentDir = QDir::currentPath() + "/agent";
    agent.setAgentDir(QDir::cleanPath(agentDir));
    agent.setPythonPath("python");

    auto issues = agent.validateConfig();

    QJsonArray arr;
    bool hasBlocking = false;
    for (const auto &i : issues) {
        QJsonObject item;
        item["key"] = i.key;
        item["message"] = i.message;
        item["blocking"] = i.blocking;
        arr.append(item);
        if (i.blocking) hasBlocking = true;
        emitHuman(QString("[%1] %2: %3")
                      .arg(i.blocking ? "阻断" : "提醒", i.key, i.message));
    }

    emitJson("config_validation", {
        {"issues", arr},
        {"all_ready", !hasBlocking},
    });

    return hasBlocking ? 1 : 0;
}

// ============================================================
// main
// ============================================================

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("Certus");
    app.setApplicationVersion("0.3.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Certus —— 独立通用 AI 研究系统（CLI 后端）\n\n"
        "外部 Agent 用法:\n"
        "  certus_backend search --json --stdin -o report.md < query.txt\n"
        "  certus_backend status --json\n"
        "  certus_backend validate --json");
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument("command",
                                 "search | browser | config | status | validate");

    // === 全局选项 ===
    QCommandLineOption jsonOpt("json", "JSON 行输出格式（供外部 Agent 解析）");
    parser.addOption(jsonOpt);

    // === 搜索选项 ===
    QCommandLineOption depthOpt("depth", "搜索深度 (L2/L3)", "depth", "L2");
    QCommandLineOption searchPlatOpt("search-platform", "搜索平台", "platform", "deepseek");
    QCommandLineOption synthPlatOpt("synthesis-platform", "整合平台", "platform", "kimi");
    QCommandLineOption cdpPortOpt("cdp-port", "CDP 端口", "port", "9223");
    QCommandLineOption mockOpt("mock-cdp", "Mock CDP 模式（无浏览器）");
    QCommandLineOption stdinOpt("stdin", "从 stdin 读取搜索问题");
    QCommandLineOption outputOpt({"o", "output"}, "报告输出路径", "path", "");

    // === 浏览器选项 ===
    QCommandLineOption startOpt("start", "启动浏览器");
    QCommandLineOption stopOpt("stop", "关闭浏览器");
    QCommandLineOption portOpt("port", "浏览器端口", "port", "9223");

    // === 配置选项 ===
    QCommandLineOption apiKeyOpt("api-key", "API Key", "key", "");

    parser.addOption(depthOpt);
    parser.addOption(searchPlatOpt);
    parser.addOption(synthPlatOpt);
    parser.addOption(cdpPortOpt);
    parser.addOption(mockOpt);
    parser.addOption(stdinOpt);
    parser.addOption(outputOpt);
    parser.addOption(startOpt);
    parser.addOption(stopOpt);
    parser.addOption(portOpt);
    parser.addOption(apiKeyOpt);

    parser.parse(app.arguments());

    g_jsonMode = parser.isSet(jsonOpt);

    const QStringList args = parser.positionalArguments();
    QString command = args.value(0);

    // === 命令路由 ===

    if (command == "search") {
        QString query = args.value(1);

        // --stdin：从标准输入读取问题
        if (parser.isSet(stdinOpt) && query.isEmpty()) {
            QTextStream in(stdin);
            query = in.readAll().trimmed();
        }

        if (query.isEmpty()) {
            emitError("usage", "用法: certus_backend search \"查询问题\" [选项]  或  echo \"问题\" | certus_backend search --stdin");
            return 1;
        }

        return cmdSearch(
            query,
            parser.value(depthOpt),
            parser.value(searchPlatOpt),
            parser.value(synthPlatOpt),
            parser.value(cdpPortOpt),
            parser.isSet(mockOpt),
            parser.value(outputOpt));

    } else if (command == "browser") {
        bool start = parser.isSet(startOpt);
        bool stop  = parser.isSet(stopOpt);
        int port = parser.value(portOpt).toInt();
        if (!start && !stop) {
            emitError("usage", "用法: certus_backend browser --start [--port PORT]  或  --stop");
            return 1;
        }
        return cmdBrowser(start, port);

    } else if (command == "config") {
        return cmdConfig(
            parser.value(cdpPortOpt),
            parser.value(apiKeyOpt));

    } else if (command == "status") {
        return cmdStatus();

    } else if (command == "validate") {
        return cmdValidate();

    } else {
        parser.showHelp(1);
    }

    return app.exec();
}
