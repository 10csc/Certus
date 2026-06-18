#include "agentmanager.h"
#include "database.h"
#include "../utils/jsonparser.h"
#include "../utils/crypto.h"
#include "../utils/logger.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonArray>
#include <QDebug>

// ============================================================
// 生命周期
// ============================================================

AgentManager::AgentManager(QObject *parent)
    : QObject(parent)
{
    m_pingTimer = new QTimer(this);
    m_heartbeatTimer = new QTimer(this);
    m_handshakeTimer = new QTimer(this);

    connect(m_pingTimer, &QTimer::timeout, this, &AgentManager::onPingTimer);
    connect(m_heartbeatTimer, &QTimer::timeout, this,
            &AgentManager::onHeartbeatTimeout);
    connect(m_handshakeTimer, &QTimer::timeout, this,
            &AgentManager::onHandshakeTimeout);

    m_heartbeatTimer->setSingleShot(true);
    m_handshakeTimer->setSingleShot(true);
}

// ============================================================
// 配置验证
// ============================================================

QList<AgentManager::ConfigIssue> AgentManager::validateConfig() const
{
    QList<ConfigIssue> issues;

    // CDP 端口
    if (m_db) {
        QString cdpPort = m_db->loadConfig("cdp_port", "");
        if (cdpPort.isEmpty()) {
            issues.append({"cdp_port", "CDP 端口未配置", true});
        }

        // API Key
        QString encryptedKey = m_db->loadConfig("deepseek_key", "");
        if (encryptedKey.isEmpty()) {
            issues.append({"deepseek_key", "DeepSeek API Key 未配置（搜索仍可进行，但本地总结和最终审阅将跳过）", false});
        } else {
            QString decrypted = Crypto::decrypt(encryptedKey);
            if (decrypted.isEmpty()) {
                issues.append({"deepseek_key", "API Key 解密失败，请在配置页重新输入", false});
            }
        }

        // Python 路径
        if (m_pythonPath.isEmpty() || m_pythonPath == "python") {
            // 尝试自动检测
            QString detected = m_db->loadConfig("python_path", "");
            if (!detected.isEmpty()) {
                // 用户已配置
            }
        }

        // Agent 目录
        QString agentDir = m_agentDir;
        if (agentDir.isEmpty()) {
            agentDir = QDir::currentPath() + "/agent";
        }
        if (!QFile::exists(QDir::cleanPath(agentDir + "/agent.py"))) {
            issues.append({"agent_dir", QString("Agent 入口不存在: %1").arg(agentDir), true});
        }
    }

    return issues;
}

AgentManager::~AgentManager()
{
    forceKill();
    releaseSearchLock();
}

void AgentManager::setDatabase(Database *db) { m_db = db; }

void AgentManager::setPythonPath(const QString &path) { m_pythonPath = path; }

void AgentManager::setAgentDir(const QString &dir) { m_agentDir = dir; }

// ============================================================
// 启动搜索
// ============================================================

bool AgentManager::start(const QString &query, const QString &depth,
                         const QString &searchPlatform,
                         const QString &synthesisPlatform,
                         const QString &cdpPort, bool mockCdp)
{
    if (m_state != Idle && m_state != Error) {
        qWarning() << "[AgentManager] 搜索进行中，不能重复启动";
        return false;
    }

    if (!acquireSearchLock()) {
        qWarning() << "[AgentManager] 已有搜索在运行（Mutex 已存在）";
        return false;
    }

    m_cdpPort = cdpPort;

    // 创建 QProcess
    if (m_process) {
        m_process->deleteLater();
    }
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    // Python Agent 路径（尝试多个候选位置）
    QString agentPy;
    QStringList candidates = {
        m_agentDir + "/agent.py",
        m_agentDir + "/../agent/agent.py",
        QDir::currentPath() + "/agent/agent.py",
    };
    for (const auto &c : candidates) {
        if (QFile::exists(QDir::cleanPath(c))) {
            agentPy = QDir::cleanPath(c);
            break;
        }
    }
    if (agentPy.isEmpty()) {
        qCritical() << "[AgentManager] Agent 入口不存在，搜索路径:"
                     << candidates;
        releaseSearchLock();
        return false;
    }

    // 启动参数
    QStringList args;
    args << agentPy << "--protocol";

    // 环境变量（UTF-8）
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUTF8", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    m_process->setProcessEnvironment(env);

    m_process->setWorkingDirectory(m_agentDir);

    // 连接信号
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &AgentManager::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &AgentManager::onReadyReadStderr);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AgentManager::onProcessFinished);

    // 启动
    qInfo() << "[AgentManager] 启动 Python Agent:" << m_pythonPath << args;
    m_process->start(m_pythonPath, args);

    if (!m_process->waitForStarted(5000)) {
        qCritical() << "[AgentManager] Python Agent 启动失败:"
                     << m_process->errorString();
        releaseSearchLock();
        return false;
    }

    setState(Handshaking);

    LOG_INFO("agent", "搜索启动: query=%.50s..., depth=%s, platforms=%s/%s",
             query.toUtf8().constData(),
             depth.toUtf8().constData(),
             searchPlatform.toUtf8().constData(),
             synthesisPlatform.toUtf8().constData());

    // 更新 session_state
    if (m_db) {
        m_db->setSessionStatus(Database::Running,
                               static_cast<int>(m_process->processId()),
                               query);

        // 插入搜索记录
        Database::SearchRecord rec;
        rec.query = query;
        rec.depth = depth;
        rec.platform = searchPlatform + "/" + synthesisPlatform;
        rec.project = m_db->loadConfig("current_project", "");
        rec.status = "running";
        m_lastSearchId = m_db->insertSearchRecord(rec);
    }

    // 发送 hello 握手（传入完整配置，从 SQLite 动态组装）
    QJsonObject config;
    config["query"] = query;
    config["depth"] = depth;
    config["search_platform"] = searchPlatform;
    config["synthesis_platform"] = synthesisPlatform;
    config["cdp_port"] = m_cdpPort;
    config["data_dir"] = QDir::currentPath() + "/data";
    config["mock_cdp"] = mockCdp;

    // 传递 Python 运行环境信息（避免 Python 回退 config.json）
    {
        QJsonObject localEnv;
        localEnv["os"] = QJsonObject{
            {"type", "windows"}, {"encoding", "utf-8"}, {"path_sep", "\\"}};
        localEnv["python"] = QJsonObject{
            {"path", m_pythonPath}, {"version", "3.12"}};
        localEnv["shell"] = QJsonObject{
            {"path", "C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"},
            {"type", "powershell"}};
        localEnv["browser"] = QJsonObject{
            {"type", "edge"}, {"path", "msedge.exe"},
            {"cdp_port", m_cdpPort.toInt()}};
        localEnv["initialized"] = true;
        config["local_env"] = localEnv;
    }

    // 从 SQLite 加载完整配置传给 Python Agent（零硬编码）
    if (m_db) {
        // 平台 URL —— 从 SQLite 读取，首次启动时回退内置默认值
        QString platformUrlsJson = m_db->loadConfig("platform_urls", "{}");
        QJsonObject platformUrls = QJsonDocument::fromJson(platformUrlsJson.toUtf8()).object();
        if (platformUrls.isEmpty()) {
            platformUrls = QJsonObject{
                {"deepseek", "https://chat.deepseek.com/"},
                {"kimi", "https://www.kimi.com/"},
                {"chatgpt", "https://chatgpt.com/"},
                {"gemini", "https://gemini.google.com/"}
            };
        }
        config["platform_urls"] = platformUrls;

        // 聊天路径特征 —— 从 SQLite 读取，首次启动时回退内置默认值
        QString patternsJson = m_db->loadConfig("chat_path_patterns", "{}");
        QJsonObject patterns = QJsonDocument::fromJson(patternsJson.toUtf8()).object();
        if (patterns.isEmpty()) {
            QJsonArray dsArr; dsArr.append(QString("/a/chat/s/"));
            patterns["deepseek"] = dsArr;
            QJsonArray kiArr; kiArr.append(QString("/chat/"));
            patterns["kimi"] = kiArr;
            QJsonArray cgArr; cgArr.append(QString("/c/"));
            patterns["chatgpt"] = cgArr;
            QJsonArray gmArr; gmArr.append(QString("/app/"));
            patterns["gemini"] = gmArr;
        }
        config["chat_path_patterns"] = patterns;

        // 会话链接 —— 从 SQLite 读取
        QString sessionsJson = m_db->loadConfig("sessions", "{}");
        QJsonObject sessions = QJsonDocument::fromJson(sessionsJson.toUtf8()).object();
        config["sessions"] = sessions;

        // 当前项目
        config["current_project"] = m_db->loadConfig("current_project", "");

        // 会话模式
        config["session_mode"] = m_db->loadConfig("session_mode", "fixed");

        // API 端点与密钥
        config["deepseek_api"] = m_db->loadConfig("deepseek_api", "https://api.deepseek.com/v1");
        {
            QString encryptedKey = m_db->loadConfig("deepseek_key", "");
            if (!encryptedKey.isEmpty()) {
                QString decrypted = Crypto::decrypt(encryptedKey);
                config["deepseek_key"] = decrypted.isEmpty() ? "" : decrypted;
            } else {
                config["deepseek_key"] = "";
            }
        }
    }

    QJsonObject hello;
    hello["action"] = "hello";
    hello["protocol"] = "1.0";
    hello["mode"] = "search";
    hello["config"] = config;

    writeToStdin(hello);

    // 启动握手超时检测（10s 内必须收到 hello_ack）
    m_handshakeTimer->start(HANDSHAKE_TIMEOUT_MS);

    // 启动心跳定时器
    m_pingTimer->start(HEARTBEAT_INTERVAL_MS);

    return true;
}

// ============================================================
// 取消
// ============================================================

void AgentManager::cancel()
{
    if (m_state != Searching) {
        return;
    }
    setState(Cancelling);

    QJsonObject cancelMsg;
    cancelMsg["action"] = "cancel";
    cancelMsg["seq"] = 1;
    cancelMsg["timestamp"] = QDateTime::currentSecsSinceEpoch();
    writeToStdin(cancelMsg);

    qInfo() << "[AgentManager] 已发送取消指令";

    // 超时保护：10s 内未收到 cancelled → 强杀
    QTimer::singleShot(CANCEL_TIMEOUT_MS, this, [this]() {
        if (m_state == Cancelling) {
            qWarning() << "[AgentManager] 取消超时，强杀进程";
            forceKill();
        }
    });
}

void AgentManager::forceKill()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        qInfo() << "[AgentManager] 强杀 Python 进程";
        m_process->kill();
        m_process->waitForFinished(3000);
    }
    m_pingTimer->stop();
    m_heartbeatTimer->stop();
    m_handshakeTimer->stop();
    releaseSearchLock();

    if (m_db) {
        m_db->setSessionStatus(Database::Idle);
    }
    setState(Error);
}

// ============================================================
// Named Mutex
// ============================================================

bool AgentManager::acquireSearchLock()
{
    m_searchMutex = CreateMutexW(
        nullptr, FALSE, L"Global\\CertusSearchMutex");

    if (m_searchMutex == nullptr) {
        qWarning() << "[AgentManager] 创建搜索互斥锁失败 → 拒绝并发搜索";
        return false;
    }

    DWORD waitResult = WaitForSingleObject(m_searchMutex, 0);
    if (waitResult == WAIT_OBJECT_0) {
        return true;  // 成功获取
    }

    // 已被占用
    CloseHandle(m_searchMutex);
    m_searchMutex = nullptr;
    return false;
}

void AgentManager::releaseSearchLock()
{
    if (m_searchMutex) {
        ReleaseMutex(m_searchMutex);
        CloseHandle(m_searchMutex);
        m_searchMutex = nullptr;
    }
}

// ============================================================
// Private: 握手
// ============================================================

void AgentManager::startHandshake()
{
    // 握手在 start() 中直接发送，此方法预留
}

// ============================================================
// Private: 写 stdin
// ============================================================

void AgentManager::writeToStdin(const QJsonObject &msg)
{
    if (!m_process || m_process->state() != QProcess::Running) {
        return;
    }
    QByteArray frame = JsonParser::encode(msg);
    m_process->write(frame);
}

// ============================================================
// Private: stdout 数据读取
// ============================================================

void AgentManager::onReadyReadStdout()
{
    m_stdoutBuffer.append(m_process->readAllStandardOutput());

    // 防内存泄漏：buffer 超过 8MB 且没有完整帧时，截断保留最后 256KB
    if (m_stdoutBuffer.size() > 8 * 1024 * 1024) {
        QList<QJsonObject> checkFrames;
        int checkCount = JsonParser::decodeStream(m_stdoutBuffer, checkFrames);
        if (checkCount == 0) {
            int keepSize = 256 * 1024;
            if (m_stdoutBuffer.size() > keepSize)
                m_stdoutBuffer = m_stdoutBuffer.right(keepSize);
        }
    }

    QList<QJsonObject> frames;
    int count = JsonParser::decodeStream(m_stdoutBuffer, frames);

    for (const auto &frame : frames) {
        processFrame(frame);
    }
}

// ============================================================
// Private: stderr 日志
// ============================================================

void AgentManager::onReadyReadStderr()
{
    QByteArray errData = m_process->readAllStandardError();
    QString errText = QString::fromUtf8(errData);

    // 累积到缓冲区（崩溃时用于诊断摘要）
    m_stderrBuffer.append(errData);
    if (m_stderrBuffer.size() > 32768) {
        m_stderrBuffer = m_stderrBuffer.right(16384);  // 只保留最后 16KB
    }

    // 写入日志文件（方便调试）
    QFile logFile("data/agent_stderr.log");
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&logFile);
        ts << errText;
    }
    qInfo().noquote() << "[Python]" << errText.trimmed();
}

// ============================================================
// Private: 帧处理
// ============================================================

void AgentManager::processFrame(const QJsonObject &frame)
{
    QString event = frame["event"].toString();
    int seq = frame["seq"].toInt(-1);

    // seq 校验
    if (!JsonParser::checkSeq(m_lastSeq, seq)) {
        // 不中断，仅 WARNING（由 checkSeq 内部输出）
    }
    m_lastSeq = seq;

    // 事件路由
    if (event == "hello_ack") {
        // 握手完成
        m_handshakeTimer->stop();
        qInfo() << "[AgentManager] 握手完成 | Agent 版本:"
                << frame["agent_version"].toString();
        setState(Searching);
        emit searchStarted();

    } else if (event == "stage_start") {
        QString stage = frame["stage"].toString();
        QString question = frame["question"].toString();
        QString platform = frame["platform"].toString();
        emit stageStarted(stage, question, platform);

    } else if (event == "stage_progress") {
        QString stage = frame["stage"].toString();
        QString platform = frame["platform"].toString();
        int elapsedSec = frame["elapsed_sec"].toInt(0);
        int contentLen = frame["content_len"].toInt(0);
        emit stageProgress(stage, platform, elapsedSec, contentLen);

    } else if (event == "stage_done") {
        QString stage = frame["stage"].toString();
        QString platform = frame["platform"].toString();
        int contentLen = frame["content_len"].toInt(0);

        emit stageFinished(stage, platform, contentLen);

        // 持久化到 SQLite
        if (m_db && m_lastSearchId > 0) {
            // 信源评分（从 stage_done 事件中提取）
            if (frame.contains("sources")) {
                QJsonArray sources = frame["sources"].toArray();
                for (const auto &s : sources) {
                    QJsonObject src = s.toObject();
                    Database::SourceScore score;
                    score.searchId = m_lastSearchId;
                    score.url = src["url"].toString();
                    score.category = src["category"].toString();
                    score.score = src["score"].toInt(5);
                    m_db->insertSourceScore(score);
                }
            }

            // 可靠性快照
            if (frame.contains("reliability")) {
                QJsonObject rel = frame["reliability"].toObject();
                Database::ReliabilitySnapshot snap;
                snap.searchId = m_lastSearchId;
                snap.platform = platform;
                snap.confirmed = rel["confirmed"].toInt(0);
                snap.inferred = rel["inferred"].toInt(0);
                snap.unconfirmed = rel["unconfirmed"].toInt(0);
                m_db->insertReliability(snap);
            }

            // 平台性能
            Database::PlatformPerf perf;
            perf.platform = platform;
            perf.stage = stage;
            perf.elapsedSec = frame["elapsed_sec"].toDouble(0);
            perf.success = (frame["status"].toString() != "timeout"
                            && frame["status"].toString() != "failed");
            m_db->insertPlatformPerf(perf);

            // 失败的阶段也记录到故障日志
            if (!perf.success) {
                Database::FailureRecord fail;
                fail.errorType = frame["status"].toString().isEmpty()
                    ? "failed" : frame["status"].toString();
                fail.platform = platform;
                fail.detail = frame["error"].toString("阶段失败");
                m_db->insertFailure(fail);
            }
        }

    } else if (event == "done") {
        // 搜索完成
        double elapsed = frame["elapsed_sec"].toDouble(0);
        QString reportPath = frame["report_path"].toString();
        int contentLen = frame["content_len"].toInt(0);

        qInfo() << "[AgentManager] 搜索完成 | 耗时:" << elapsed
                << "s | 报告:" << reportPath;

        // 更新 SQLite
        if (m_db && m_lastSearchId > 0) {
            m_db->updateSearchStatus(m_lastSearchId, "done");
            m_db->setSessionStatus(Database::Idle);
        }

        // 更新搜索记录（通过 Database 封装，不走直接 SQL）
        if (m_db && m_lastSearchId > 0) {
            m_db->updateSearchDetails(m_lastSearchId, elapsed, contentLen, reportPath);

            // 自动创建知识条目（带 24h 去重）
            auto searches = m_db->recentSearches(1);
            if (!searches.isEmpty() && searches.first().id == m_lastSearchId) {
                Database::KnowledgeEntry entry;
                entry.topic = searches.first().query;
                if (!reportPath.isEmpty()) {
                    QFile reportFile(reportPath);
                    if (reportFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        QString content = QString::fromUtf8(reportFile.readAll());
                        reportFile.close();
                        QString summary = content.left(500)
                            .remove(QRegularExpression("[#*`~>\\[\\]]"))
                            .simplified();
                        entry.conclusion = summary;
                    }
                }
                if (entry.conclusion.isEmpty())
                    entry.conclusion = searches.first().query;
                entry.sources = searches.first().platform;
                m_db->insertKnowledgeIfNew(entry);
            }

            // === 缓存系统：搜索完成后自动存入 ChromaDB ===
            if (!reportPath.isEmpty()) {
                auto searches2 = m_db->recentSearches(1);
                if (!searches2.isEmpty()) {
                    const auto &s = searches2.first();
                    QJsonObject cacheStoreMsg;
                    cacheStoreMsg["action"] = "cache_store";
                    cacheStoreMsg["query"] = s.query;
                    cacheStoreMsg["report_path"] = reportPath;
                    cacheStoreMsg["project"] = m_db->loadConfig("current_project", "");
                    cacheStoreMsg["platform"] = s.platform;
                    cacheStoreMsg["depth"] = s.depth;
                    cacheStoreMsg["content_len"] = contentLen;
                    cacheStoreMsg["elapsed_sec"] = elapsed;
                    cacheStoreMsg["search_history_id"] = static_cast<int>(s.id);
                    cacheStoreMsg["seq"] = 1;
                    cacheStoreMsg["timestamp"] = QDateTime::currentSecsSinceEpoch();
                    writeToStdin(cacheStoreMsg);
                }
            }
        }

        // 清理
        m_pingTimer->stop();
        m_heartbeatTimer->stop();
        releaseSearchLock();
        setState(Idle);
        emit searchFinished(true, reportPath);

    } else if (event == "pong") {
        // 心跳响应 → 重置超时计时器
        m_heartbeatTimer->stop();
        qInfo() << "[AgentManager] 收到 pong";

    } else if (event == "cancelled") {
        QString partial = frame["partial_result"].toString();
        qInfo() << "[AgentManager] 搜索已取消";

        if (m_db && m_lastSearchId > 0) {
            m_db->updateSearchStatus(m_lastSearchId, "cancelled");
            m_db->setSessionStatus(Database::Idle);
        }

        m_pingTimer->stop();
        m_heartbeatTimer->stop();
        releaseSearchLock();
        setState(Idle);
        emit searchCancelled(partial);

    } else if (event == "error") {
        QString errorType = frame["error_type"].toString();
        QString platform = frame["platform"].toString();
        QString detail = frame["detail"].toString();

        qWarning() << "[AgentManager] 搜索错误:" << errorType << platform;

        // 写入 failure_log
        if (m_db) {
            Database::FailureRecord fail;
            fail.errorType = errorType;
            fail.platform = platform;
            fail.detail = detail;
            m_db->insertFailure(fail);
        }

        emit errorOccurred(errorType, platform, detail);

    } else if (event == "cache_hit") {
        // 缓存命中
        QJsonArray matches = frame["matches"].toArray();
        qInfo() << "[AgentManager] 缓存命中:" << matches.size() << "条";
        emit cacheHit(matches);

    } else if (event == "cache_miss") {
        // 缓存未命中
        qInfo() << "[AgentManager] 缓存未命中";
        emit cacheMiss();

    } else if (event == "cache_stored") {
        // 缓存存入完成
        QString docId = frame["id"].toString();
        qInfo() << "[AgentManager] 缓存已存入:" << docId;
        emit cacheStored(docId);

    } else {
        // 未知事件 → 忽略（向前兼容）
        qInfo() << "[AgentManager] 未知事件类型:" << event << "(忽略)";
    }
}

// ============================================================
// Private: 进程结束
// ============================================================

void AgentManager::onProcessFinished(int exitCode,
                                     QProcess::ExitStatus status)
{
    qInfo() << "[AgentManager] Python 进程退出 | exitCode:" << exitCode
            << "| 状态:" << (status == QProcess::CrashExit ? "Crash" : "Normal");

    m_pingTimer->stop();
    m_heartbeatTimer->stop();
    m_handshakeTimer->stop();
    releaseSearchLock();

    // 异常退出检测
    if (exitCode != 0 || status == QProcess::CrashExit) {
        // 兜底计时：从 search_history 的 started_at 计算到当前的耗时
        if (m_db && m_lastSearchId > 0) {
            auto searches = m_db->recentSearches(1);
            if (!searches.isEmpty() && searches.first().id == m_lastSearchId) {
                QDateTime started = QDateTime::fromString(
                    searches.first().startedAt, Qt::ISODate);
                double crashElapsed = started.secsTo(QDateTime::currentDateTime());
                // 记录兜底耗时到 platform_perf
                Database::PlatformPerf perf;
                perf.platform = "system";
                perf.stage = "crash";
                perf.elapsedSec = crashElapsed;
                perf.success = false;
                m_db->insertPlatformPerf(perf);
                // 记录崩溃到故障日志
                Database::FailureRecord fail;
                fail.errorType = "crash";
                fail.platform = "system";

                // 提取 stderr 最后 5 行作为诊断摘要
                QString stderrTail;
                if (!m_stderrBuffer.isEmpty()) {
                    QStringList lines = QString::fromUtf8(m_stderrBuffer).split("\n");
                    int startIdx = qMax(0, lines.size() - 5);
                    stderrTail = lines.mid(startIdx).join("\n").trimmed();
                }

                fail.detail = QString("exit=%1 status=%2 已运行%3s")
                    .arg(exitCode)
                    .arg(status == QProcess::CrashExit ? "Crash" : "Normal")
                    .arg(crashElapsed, 0, 'f', 0);
                if (!stderrTail.isEmpty()) {
                    fail.detail += "\n--- stderr 最后5行 ---\n" + stderrTail;
                }
                m_db->insertFailure(fail);

                // 清空缓冲区（下次搜索重新开始）
                m_stderrBuffer.clear();
            }
            m_db->setSessionStatus(Database::Idle);
        }
        checkEvolution(exitCode);
    }

    if (m_state != Idle) {
        setState(Error);
    }
}

// ============================================================
// Private: 心跳
// ============================================================

void AgentManager::onPingTimer()
{
    QJsonObject ping;
    ping["action"] = "ping";
    ping["seq"] = 1;
    ping["timestamp"] = QDateTime::currentSecsSinceEpoch();
    writeToStdin(ping);

    // 启动超时计时器
    m_heartbeatTimer->start(HEARTBEAT_TIMEOUT_MS);
}

void AgentManager::onHandshakeTimeout()
{
    qWarning() << "[AgentManager] 握手超时（10s 无 hello_ack）→ 强杀 Python";
    m_handshakeTimer->stop();
    forceKill();
    emit errorOccurred("handshake_timeout", "system",
                       "Python Agent 握手超时（10s 无响应），进程已被终止");
}

// ============================================================
// 缓存系统
// ============================================================

void AgentManager::sendCacheQuery(const QString &query, const QString &project,
                                  int topK, double minSimilarity)
{
    if (m_state != Idle && m_state != Searching) {
        qWarning() << "[AgentManager] 无法发送缓存查询：当前状态不允许";
        return;
    }

    QJsonObject msg;
    msg["action"] = "cache_query";
    msg["query"] = query;
    msg["project"] = project;
    msg["top_k"] = topK;
    msg["min_similarity"] = minSimilarity;
    msg["seq"] = 1;
    msg["timestamp"] = QDateTime::currentSecsSinceEpoch();
    writeToStdin(msg);
}

void AgentManager::onHeartbeatTimeout()
{
    qWarning() << "[AgentManager] 心跳超时（35s 无 pong），判定僵死 → 强杀";
    forceKill();
    emit errorOccurred("heartbeat_timeout", "system",
                       "Python Agent 心跳超时，进程已强杀");
}

// ============================================================
// Private: 进化检测
// ============================================================

void AgentManager::checkEvolution(int exitCode)
{
    if (!m_db) return;

    QList<Database::FailureRecord> failures = m_db->recentFailures(24);
    if (!failures.isEmpty()) {
        qInfo() << "[AgentManager] 检测到" << failures.size()
                << "条最近错误记录 (exit=" << exitCode << ")";
        emit evolutionNeeded(
            QString("进程异常退出 (exit=%1)，最近 %2 条错误记录——请前往「辅助修复」页诊断")
                .arg(exitCode).arg(failures.size()));
    }
}

// ============================================================
// Private: 状态管理
// ============================================================

void AgentManager::setState(State s)
{
    if (m_state != s) {
        m_state = s;
        emit stateChanged(s);
    }
}
