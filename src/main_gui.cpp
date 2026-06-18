/**
 * Certus GUI 入口。
 *
 * 启动 Qt 桌面应用，连接后端核心（AgentManager + Database）。
 */

#include <QApplication>
#include <QDir>
#include <QMessageBox>

#include <windows.h>

#include "mainwindow.h"
#include "core/agentmanager.h"
#include "core/database.h"
#include "core/browsermanager.h"
#include "utils/logger.h"

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
    app.setStyle("Fusion");  // 暗色主题基础

    // 暗色调色板
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(30, 30, 30));
    darkPalette.setColor(QPalette::WindowText, QColor(204, 204, 204));
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(35, 35, 35));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::ToolTipText, QColor(204, 204, 204));
    darkPalette.setColor(QPalette::Text, QColor(204, 204, 204));
    darkPalette.setColor(QPalette::Button, QColor(35, 35, 35));
    darkPalette.setColor(QPalette::ButtonText, QColor(204, 204, 204));
    darkPalette.setColor(QPalette::BrightText, QColor(255, 0, 0));
    darkPalette.setColor(QPalette::Link, QColor(79, 195, 247));
    darkPalette.setColor(QPalette::Highlight, QColor(0, 120, 212));
    darkPalette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    app.setPalette(darkPalette);

    // 初始化日志系统（路径相对于 exe 目录，兼容任意启动位置）
    QString logDir = QCoreApplication::applicationDirPath() + "/logs";
    Logger::instance()->setLogDir(logDir);
    LOG_INFO("main", "Certus GUI 启动 v%s", app.applicationVersion().toUtf8().constData());

    // 数据库路径：从 exe 向上找项目根目录（兼容 build/Release 和安装目录）
    QString dbPath;
    QDir baseDir(QCoreApplication::applicationDirPath());
    for (int up = 0; up <= 2; up++) {
        QString candidate = QDir::cleanPath(baseDir.absolutePath() + "/data/certus.db");
        if (QFile::exists(candidate)) {
            dbPath = candidate;
            break;
        }
        if (!baseDir.cdUp()) break;
    }
    if (dbPath.isEmpty()) {
        dbPath = QDir::currentPath() + "/data/certus.db";  // 兜底
    }
    Database db(dbPath);
    if (!db.open()) {
        qWarning() << "无法打开数据库，部分功能不可用";
    }

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

    // Agent 目录：从 exe 位置向上查找，兼容 build/Release 和安装目录
    QString agentDir;
    QDir agentBaseDir(QCoreApplication::applicationDirPath());
    // 尝试: exe同级/agent → 上1级/agent → 上2级/agent（build/Release → 项目根）
    for (int up = 0; up <= 2; up++) {
        QString candidate = QDir::cleanPath(agentBaseDir.absolutePath() + "/agent");
        if (QFile::exists(candidate + "/agent.py")) {
            agentDir = candidate;
            break;
        }
        if (!agentBaseDir.cdUp()) break;
    }
    if (agentDir.isEmpty()) {
        agentDir = QDir::currentPath() + "/agent";  // 兜底
    }
    agent.setAgentDir(QDir::cleanPath(agentDir));
    agent.setPythonPath("python");

    // 自动启动浏览器（如果用户开启了自启动）
    if (db.loadConfig("auto_launch_browser", "0") == "1") {
        int port = db.loadConfig("cdp_port", "9223").toInt();
        browser.launch(port);
    }

    // 主窗口
    MainWindow window;
    window.setDatabase(&db);
    window.setAgentManager(&agent);
    window.setBrowserManager(&browser);
    window.setMemoryConfig("python", agentDir);
    window.show();

    return app.exec();
}
