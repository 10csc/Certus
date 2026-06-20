#include "browsermanager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>

// ============================================================
// 生命周期
// ============================================================

BrowserManager::BrowserManager(QObject *parent)
    : QObject(parent)
{
    m_jobObject = createJobObject();
}

BrowserManager::~BrowserManager()
{
    shutdownAll();
    if (m_jobObject) {
        CloseHandle(m_jobObject);
    }
}

// ============================================================
// CDP 端口扫描
// ============================================================

bool BrowserManager::isCdpAvailable(int port)
{
    QString url = QString("http://127.0.0.1:%1/json/version").arg(port);

    QNetworkAccessManager mgr;
    mgr.setTransferTimeout(2000);  // 2s 超时 (Qt 6.5+)
    QUrl qUrl(url);
    QNetworkRequest req(qUrl);

    QNetworkReply *reply = mgr.get(req);

    // 同步等待
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return false;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return false;

    QString wsUrl = doc.object()["webSocketDebuggerUrl"].toString();
    return !wsUrl.isEmpty();
}

int BrowserManager::scanCdpPort(int startPort, int endPort)
{
    for (int port = startPort; port <= endPort; port++) {
        if (isCdpAvailable(port)) {
            return port;
        }
    }
    return 0;
}

// ============================================================
// 浏览器路径
// ============================================================

QString BrowserManager::findBrowserPath(BrowserType type)
{
    // Edge（优先）
    static const char *edgePaths[] = {
        "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
    };
    // Chrome
    static const char *chromePaths[] = {
        "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
    };

    if (type == Edge || type == Auto) {
        for (const char *p : edgePaths) {
            if (QFile::exists(p)) return QString::fromUtf8(p);
        }
    }
    if (type == Chrome || type == Auto) {
        for (const char *p : chromePaths) {
            if (QFile::exists(p)) return QString::fromUtf8(p);
        }
    }
    return {};
}

// ============================================================
// 启动浏览器
// ============================================================

int BrowserManager::launch(int port, BrowserType type)
{
    QString browserPath = findBrowserPath(type);
    if (browserPath.isEmpty()) {
        emit errorOccurred("未找到 Edge/Chrome 浏览器");
        return 0;
    }

    // 确定端口
    int usePort = port > 0 ? port : 9223;

    // 检查端口是否已被占用
    if (isCdpAvailable(usePort)) {
        qInfo() << "[Browser] CDP 端口" << usePort << "已被占用，复用现有浏览器";
        m_managedPorts.append(usePort);
        emit browserLaunched(usePort);
        return usePort;
    }

    // 启动浏览器
    QProcess *proc = new QProcess(this);
    QStringList args;
    args << ("--remote-debugging-port=" + QString::number(usePort));
    args << "--no-first-run";
    args << "--no-default-browser-check";

    qInfo() << "[Browser] 启动:" << browserPath << "CDP:" << usePort;
    proc->start(browserPath, args);

    if (!proc->waitForStarted(5000)) {
        qWarning() << "[Browser] 启动失败:" << proc->errorString();
        delete proc;
        return 0;
    }

    // 绑定到 Job Object
    if (m_jobObject) {
        HANDLE procHandle =
            OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE,
                        FALSE, static_cast<DWORD>(proc->processId()));
        if (procHandle) {
            bindToJobObject(procHandle);
            CloseHandle(procHandle);
        }
    }

    m_browserProcesses.append(proc);
    m_managedPorts.append(usePort);

    // 等待 CDP 就绪（浏览器冷启动最多等 20s）
    for (int i = 0; i < 20; i++) {
        Sleep(1000);
        QCoreApplication::processEvents();
        if (isCdpAvailable(usePort)) {
            qInfo() << "[Browser] CDP" << usePort << "就绪";
            emit browserLaunched(usePort);
            return usePort;
        }
    }

    qWarning() << "[Browser] CDP" << usePort << "超时(20s)未就绪 → 清理进程";
    bool stillAlive = (proc->state() == QProcess::Running);
    proc->kill();
    proc->waitForFinished(3000);
    m_browserProcesses.removeOne(proc);
    m_managedPorts.removeOne(usePort);
    delete proc;
    return 0;
}

// ============================================================
// 关闭浏览器
// ============================================================

bool BrowserManager::shutdown(int port)
{
    for (int i = 0; i < m_browserProcesses.size(); i++) {
        QProcess *proc = m_browserProcesses[i];
        if (proc && m_managedPorts.value(i) == port) {
            // 优雅关闭
            proc->terminate();
            if (!proc->waitForFinished(5000)) {
                // Fallback 强杀
                qWarning() << "[Browser] 优雅关闭超时，强杀端口" << port;
                proc->kill();
                proc->waitForFinished(3000);
            }
            qInfo() << "[Browser] 端口" << port << "已关闭";
            emit browserClosed(port);
            return true;
        }
    }
    return false;
}

void BrowserManager::shutdownAll()
{
    for (auto *proc : m_browserProcesses) {
        if (proc && proc->state() != QProcess::NotRunning) {
            proc->terminate();
            proc->waitForFinished(5000);
            if (proc->state() != QProcess::NotRunning) {
                proc->kill();
                proc->waitForFinished(3000);
            }
        }
        delete proc;
    }
    m_browserProcesses.clear();
    m_managedPorts.clear();
}

bool BrowserManager::isRunning() const
{
    for (auto *proc : m_browserProcesses) {
        if (proc && proc->state() == QProcess::Running) {
            return true;
        }
    }
    return false;
}

// ============================================================
// Job Object 管理
// ============================================================

HANDLE BrowserManager::createJobObject()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
    jeli.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;  // C++ 进程退出 → OS 自动杀子进程

    SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                            &jeli, sizeof(jeli));
    return job;
}

bool BrowserManager::bindToJobObject(HANDLE processHandle)
{
    if (!m_jobObject) return false;
    return AssignProcessToJobObject(m_jobObject, processHandle);
}
