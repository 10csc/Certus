#pragma once
/**
 * CDP 浏览器生命周期管理器。
 *
 * 职责：
 *   - 扫描端口 9223-9226，检测可用 CDP 浏览器
 *   - 启动 Edge/Chrome（--remote-debugging-port=N）
 *   - Job Object 绑定浏览器进程 → C++ 崩溃时 OS 自动清理
 *   - 复用已有 CDP 连接
 *   - 优雅关闭 + fallback 强杀
 */

#include <QObject>
#include <QString>
#include <QProcess>
#include <windows.h>

class BrowserManager : public QObject {
    Q_OBJECT

public:
    enum BrowserType { Edge, Chrome, Auto };

    explicit BrowserManager(QObject *parent = nullptr);
    ~BrowserManager();

    /// 扫描 CDP 端口范围，返回第一个可用端口号（0 = 无可用）
    int scanCdpPort(int startPort = 9223, int endPort = 9226);

    /// 启动浏览器并返回 CDP 端口（0 = 失败）
    int launch(int port = 0, BrowserType type = Edge);

    /// 关闭指定端口的浏览器
    bool shutdown(int port);

    /// 关闭所有由此管理器启动的浏览器
    void shutdownAll();

    /// 检测指定端口是否可用 CDP
    bool isCdpAvailable(int port);

    /// 获取浏览器可执行文件路径
    static QString findBrowserPath(BrowserType type = Edge);

    /// 当前是否管理着浏览器
    bool isRunning() const;

signals:
    void browserLaunched(int cdpPort);
    void browserClosed(int cdpPort);
    void errorOccurred(const QString &error);

private:
    /// Job Object 绑定：将进程绑定到 C++ 进程树
    bool bindToJobObject(HANDLE processHandle);
    HANDLE createJobObject();

    QList<QProcess *> m_browserProcesses;
    QList<int> m_managedPorts;
    HANDLE m_jobObject = nullptr;
};
