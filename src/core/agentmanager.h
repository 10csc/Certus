#pragma once
/**
 * Python Agent 进程管理器。
 *
 * 职责：
 *   - spawn Python 子进程（QProcess）—— 搜索用 agent.py
 *   - 启动握手（hello → hello_ack，从 SQLite 组装完整配置传入）
 *   - 读取 stdout JSON 帧 → 解析事件 → 写入 SQLite / 发射信号
 *   - 心跳检测（30s ping，35s 无 pong → 强杀）
 *   - 取消搜索（发 cancel 指令）
 *   - 崩溃检测 → 发射 evolutionNeeded 信号（提示用户前往 RepairPage 手动诊断）
 *   - Windows Named Mutex 防并发
 */

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <windows.h>

class Database;

class AgentManager : public QObject {
    Q_OBJECT

public:
    enum State {
        Idle,           // 空闲
        Handshaking,    // 等待 hello_ack
        Searching,      // 搜索中
        Cancelling,     // 等待取消确认
        Error,          // 错误
    };

    explicit AgentManager(QObject *parent = nullptr);
    ~AgentManager();

    // === 生命周期 ===

    void setDatabase(Database *db);
    void setPythonPath(const QString &path);
    void setAgentDir(const QString &dir);
    void setDataDir(const QString &dir);

    /// 启动搜索
    bool start(const QString &query,
               const QString &depth = "L2",
               const QString &searchPlatform = "deepseek",
               const QString &synthesisPlatform = "deepseek",
               const QString &cdpPort = "9223",
               bool mockCdp = false);

    /// 取消当前操作
    void cancel();

    /// 强制终止（心跳超时 / 手动强杀）
    void forceKill();

    /// 获取当前状态
    State state() const { return m_state; }

    // === 配置验证 ===

    struct ConfigIssue {
        QString key;
        QString message;
        bool blocking = false;  // 阻断搜索
    };

    /// 检查当前配置是否完整可用（非阻塞，纯读取）
    QList<ConfigIssue> validateConfig() const;

    // === Named Mutex ===

    /// 尝试获取全局搜索锁（防多实例并发）
    bool acquireSearchLock();
    void releaseSearchLock();

    // === 缓存系统 ===

    /// 发送缓存查询指令到 Python 端（搜索前调用）
    void sendCacheQuery(const QString &query,
                        int topK = 3, double minSimilarity = 0.85);

signals:
    // 搜索生命周期
    void searchStarted();
    void searchFinished(bool success, const QString &reportPath);
    void searchCancelled(const QString &partialResult);

    // 阶段事件
    void stageStarted(const QString &stage, const QString &question,
                      const QString &platform);
    void stageFinished(const QString &stage, const QString &platform,
                       int contentLen);
    void stageProgress(const QString &stage, const QString &platform,
                       int elapsedSec, int contentLen);

    // 错误
    void errorOccurred(const QString &errorType, const QString &platform,
                       const QString &detail);
    void evolutionNeeded(const QString &reason);

    // 缓存系统
    void cacheHit(const QJsonArray &matches);   // 缓存命中
    void cacheMiss();                            // 缓存未命中
    void cacheStored(const QString &docId);      // 缓存存入完成

    // 状态变更
    void stateChanged(AgentManager::State newState);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onHeartbeatTimeout();
    void onPingTimer();
    void onHandshakeTimeout();

private:
    void startHandshake();
    void processFrame(const QJsonObject &frame);
    void writeToStdin(const QJsonObject &msg);
    void setState(State s);
    void checkEvolution(int exitCode);
    bool spawnProcess(const QString &scriptName,
                      const QJsonObject &helloConfig);

    QProcess *m_process = nullptr;
    Database *m_db = nullptr;
    QTimer *m_pingTimer = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_handshakeTimer = nullptr;     // 握手超时检测

    QString m_pythonPath = "python";
    QString m_agentDir;
    QString m_dataDir;    // 项目根/data（由 main_gui 传入）
    QString m_cdpPort;

    State m_state = Idle;
    int m_lastSeq = -1;
    int m_lastSearchId = -1;

    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;              // 累积 stderr，崩溃时提取最后5行

    // Named Mutex
    HANDLE m_searchMutex = nullptr;

    // 常量
    static constexpr int HANDSHAKE_TIMEOUT_MS   = 15000;   // 15s 等 hello_ack
    static constexpr int HEARTBEAT_INTERVAL_MS  = 30000;   // 30s 发一次 ping
    static constexpr int HEARTBEAT_TIMEOUT_MS   = 35000;   // 35s 无 pong → 强杀
    static constexpr int CANCEL_TIMEOUT_MS      = 10000;   // 10s 等 cancelled
};
