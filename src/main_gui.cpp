/**
 * Certus GUI 入口。
 *
 * 启动 Qt 桌面应用，连接后端核心（AgentManager + Database）。
 */

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QMessageBox>

#include <windows.h>

#include "mainwindow.h"
#include "core/agentmanager.h"
#include "core/database.h"
#include "core/browsermanager.h"
#include "utils/logger.h"
#include "ui/theme.h"

int main(int argc, char *argv[])
{
    // 单实例保护：防止多实例并行造成 IPC/SQLite 冲突
    HANDLE hSingleInstance = CreateMutexW(
        nullptr, FALSE, L"Global\\CertusSingleInstance");
    if (hSingleInstance == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hSingleInstance) CloseHandle(hSingleInstance);
        // Qt 还没初始化，用 Windows MessageBox
        MessageBoxW(nullptr,
            L"Certus 已在运行中。\n\n请关闭已有实例后再启动新实例。",
            L"Certus — 单实例限制",
            MB_OK | MB_ICONWARNING);
        return 1;
    }

    QApplication app(argc, argv);
    app.setApplicationName("Certus");
    app.setApplicationVersion("0.3.0");
    app.setStyle("Fusion");

    // 初始化日志系统（必须在 qInstallMessageHandler 之前）
    QString logDir = QCoreApplication::applicationDirPath() + "/logs";
    Logger::instance()->setLogDir(logDir);

    // 安装 Qt 消息处理器：将 qWarning/qCritical/qDebug 写入日志文件
    // 解决根目录 exe 找不到 SQLite 插件时静默失败的问题
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx,
                              const QString &msg) {
        Logger::Level level;
        const char *label;
        switch (type) {
        case QtDebugMsg:   level = Logger::Debug;   label = "DBG";  break;
        case QtInfoMsg:    level = Logger::Info;    label = "INF";  break;
        case QtWarningMsg: level = Logger::Warning; label = "WRN";  break;
        case QtCriticalMsg:level = Logger::Error;   label = "ERR";  break;
        case QtFatalMsg:   level = Logger::Error;   label = "FATAL";break;
        default:           level = Logger::Debug;   label = "???";  break;
        }
        Logger::instance()->log(level, "Qt", "%s: %s", label, msg.toUtf8().constData());
    });

    // 亮色调色板
    QPalette lightPalette;
    lightPalette.setColor(QPalette::Window, QColor(250, 250, 250));
    lightPalette.setColor(QPalette::WindowText, QColor(26, 26, 26));
    lightPalette.setColor(QPalette::Base, QColor(255, 255, 255));
    lightPalette.setColor(QPalette::AlternateBase, QColor(240, 240, 240));
    lightPalette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    lightPalette.setColor(QPalette::ToolTipText, QColor(26, 26, 26));
    lightPalette.setColor(QPalette::Text, QColor(26, 26, 26));
    lightPalette.setColor(QPalette::Button, QColor(240, 240, 240));
    lightPalette.setColor(QPalette::ButtonText, QColor(26, 26, 26));
    lightPalette.setColor(QPalette::BrightText, QColor(255, 0, 0));
    lightPalette.setColor(QPalette::Link, QColor(37, 99, 235));
    lightPalette.setColor(QPalette::Highlight, QColor(37, 99, 235));
    lightPalette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    app.setPalette(lightPalette);
    app.setStyleSheet(globalStyleSheet());  // 加载全局 QSS 主题

    auto t0 = QDateTime::currentMSecsSinceEpoch();
    LOG_INFO("main", "Certus GUI 启动 v%s", app.applicationVersion().toUtf8().constData());

    // 定位项目根目录：向上查找含 agent/agent.py 的目录
    QDir projectRoot(QCoreApplication::applicationDirPath());
    for (int up = 0; up <= 3; up++) {
        if (QFile::exists(projectRoot.absolutePath() + "/agent/agent.py")) {
            break;
        }
        if (!projectRoot.cdUp()) {
            // 找不到则回退到 exe 所在目录
            projectRoot = QDir(QCoreApplication::applicationDirPath());
            break;
        }
    }
    QString dbPath = QDir::cleanPath(projectRoot.absolutePath() + "/data/certus.db");
    // 也检查当前工作目录（开发时可能直接 cd 到项目根启动）
    QString cwdDbPath = QDir::currentPath() + "/data/certus.db";
    if (QFile::exists(cwdDbPath) && cwdDbPath != dbPath && !QFile::exists(dbPath)) {
        dbPath = cwdDbPath;
    }

    // 设置应用图标
    QString iconPath = projectRoot.absolutePath() + "/resources/certus.png";
    if (QFile::exists(iconPath)) {
        app.setWindowIcon(QIcon(iconPath));
    }

    auto t1 = QDateTime::currentMSecsSinceEpoch();
    LOG_INFO("main", "DB path resolve: %lld ms", t1 - t0);

    Database db(dbPath);
    if (!db.open()) {
        LOG_ERROR("main", "数据库打开失败: %s", dbPath.toUtf8().constData());
        QMessageBox::critical(nullptr, "Certus — 数据库错误",
            QString("无法打开或创建数据库文件:\n\n%1\n\n"
                    "可能原因:\n"
                    "1. exe 所在目录缺少 Qt SQLite 插件 (sqldrivers/qsqlite.dll)\n"
                    "2. 磁盘空间不足或 data 目录无写入权限\n\n"
                    "请从 build/Release/ 目录启动，或运行部署脚本。")
                .arg(dbPath));
        return 1;
    }

    LOG_INFO("main", "数据库已打开: %s", dbPath.toUtf8().constData());

    auto t2 = QDateTime::currentMSecsSinceEpoch();
    LOG_INFO("main", "DB open: %lld ms", t2 - t1);

    // 启动时崩溃检测：session_state 残留 running → 上次异常退出
    Database::SessionStatus status = db.getSessionStatus();
    if (status == Database::Running) {
        qWarning() << "检测到上次异常退出 (session_state=running)，触发进化检测";
        db.setSessionStatus(Database::Idle);
        // 检查是否有故障记录 → 提示用户前往「辅助修复」页
        auto failures = db.recentFailures(24);
        if (!failures.isEmpty()) {
            qInfo() << "发现" << failures.size() << "条最近故障记录，建议前往「辅助修复」页诊断";
        }
    } else if (status == Database::Evolving) {
        qWarning() << "上次进化过程也崩溃了 → 跳过进化，重置为 idle";
        db.setSessionStatus(Database::Idle);
    }

    // 初始化浏览器管理器
    BrowserManager browser;

    // 初始化 AgentManager
    AgentManager agent;
    agent.setDatabase(&db);

    // Agent 目录：复用上面定位的 projectRoot
    QString agentDir = QDir::cleanPath(projectRoot.absolutePath() + "/agent");
    agent.setAgentDir(QDir::cleanPath(agentDir));
    agent.setPythonPath(db.loadConfig("python_path", "python"));
    // data_dir：从 agentDir 推导项目根（agentDir 以 /agent 结尾），保证路径与 Python DATA_DIR 一致
    agent.setDataDir(QDir::cleanPath(agentDir + "/../data"));

    // 自动启动浏览器（如果用户开启了自启动）
    if (db.loadConfig("auto_launch_browser", "0") == "1") {
        int port = db.loadConfig("cdp_port", "9223").toInt();
        browser.launch(port);
    }

    auto t3 = QDateTime::currentMSecsSinceEpoch();
    LOG_INFO("main", "Browser+Agent init: %lld ms", t3 - t2);

    // 主窗口
    MainWindow window;
    auto t4 = QDateTime::currentMSecsSinceEpoch();
    LOG_INFO("main", "MainWindow ctor: %lld ms", t4 - t3);

    window.setDatabase(&db);
    window.setAgentManager(&agent);
    window.setBrowserManager(&browser);
    window.setMemoryConfig(db.loadConfig("python_path", "python"), agentDir);
    auto t5 = QDateTime::currentMSecsSinceEpoch();
    LOG_INFO("main", "Window set*: %lld ms", t5 - t4);

    window.show();
    auto t6 = QDateTime::currentMSecsSinceEpoch();
    LOG_INFO("main", "Window show: %lld ms", t6 - t5);
    LOG_INFO("main", "启动总耗时: %lld ms", t6 - t0);

    return app.exec();
}
