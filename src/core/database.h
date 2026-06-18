#pragma once
/**
 * SQLite 数据库层 —— C++ 独占写入。
 *
 * 设计原则：
 *   - Python 不碰 SQLite，所有数据通过 on_event → C++ → SQLite
 *   - WAL 模式（高并发读、单写无锁竞争）
 *   - 每次连接时执行数据保留策略
 *   - session_state 永远只有一行（防并发 + 崩溃检测）
 *   - API key 通过 Crypto::encrypt 加密后存储
 */

#include <QString>
#include <QDateTime>
#include <QVariantMap>
#include <QJsonObject>
#include <QList>
#include <QSqlDatabase>
#include <QSqlQuery>

class Database {
public:
    // === 生命周期 ===

    explicit Database(const QString &dbPath);
    ~Database();

    bool open();
    void close();
    bool isOpen() const;

    // === 会话状态 ===

    enum SessionStatus { Idle, Running, Evolving };

    SessionStatus getSessionStatus();
    bool setSessionStatus(SessionStatus status,
                          int pid = 0,
                          const QString &searchQuery = {});

    // === 搜索记录 ===

    struct SearchRecord {
        qint64 id = 0;
        QString query;
        QString depth;
        QString platform;
        QString project;
        QString startedAt;
        double elapsedSec = 0;
        int contentLen = 0;
        QString reportPath;
        QString status;  // "done" | "cancelled" | "error"
    };

    qint64 insertSearchRecord(const SearchRecord &rec);
    bool updateSearchStatus(qint64 id, const QString &status);
    bool updateSearchDetails(qint64 id, double elapsedSec, int contentLen,
                             const QString &reportPath);
    QList<SearchRecord> recentSearches(int limit = 20,
                                       const QString &filter = {},
                                       const QString &project = {});

    // === 可靠性快照 ===

    struct ReliabilitySnapshot {
        qint64 searchId = 0;
        QString platform;
        int confirmed = 0;
        int inferred = 0;
        int unconfirmed = 0;
    };

    bool insertReliability(const ReliabilitySnapshot &snap);

    // === 信源评分 ===

    struct SourceScore {
        qint64 searchId = 0;
        QString url;
        QString category;
        int score = 0;
    };

    bool insertSourceScore(const SourceScore &score);

    // === 平台性能 ===

    struct PlatformPerf {
        QString platform;
        QString stage;       // "search" | "synthesis"
        double elapsedSec = 0;
        bool success = true;
    };

    bool insertPlatformPerf(const PlatformPerf &perf);

    // === 进化状态 ===

    struct EvolutionChange {
        QString platform;
        QString key;
        QString oldValue;
        QString newValue;
    };

    bool insertEvolutionChange(const EvolutionChange &change);
    QList<EvolutionChange> recentEvolutionChanges(int limit = 20);

    // === 失败记录 ===

    struct FailureRecord {
        QString errorType;   // send_failed | extract_failed | cdp_failed | timeout | evolution_failed
        QString platform;
        QString detail;
    };

    bool insertFailure(const FailureRecord &fail);
    QList<FailureRecord> recentFailures(int hours = 24);

    // === 配置 ===

    bool saveConfig(const QString &key, const QString &value);
    QString loadConfig(const QString &key, const QString &defaultValue = {});

    // === 记忆 ===

    struct KnowledgeEntry {
        qint64 id = 0;
        QString topic;
        QString conclusion;
        QString sources;
        QString createdAt;
    };

    bool insertKnowledge(const KnowledgeEntry &entry);
    bool insertKnowledgeIfNew(const KnowledgeEntry &entry);
    KnowledgeEntry getKnowledgeById(qint64 id);
    bool updateKnowledge(qint64 id, const KnowledgeEntry &entry);
    bool deleteKnowledge(qint64 id);
    QList<KnowledgeEntry> searchKnowledge(const QString &keyword);
    QList<KnowledgeEntry> listKnowledge(int limit = 100, int offset = 0);

    // === 监控聚合 ===

    struct ReliabilityAggregate {
        QString platform;
        int totalConfirmed = 0;
        int totalInferred = 0;
        int totalUnconfirmed = 0;
        int snapshotCount = 0;
    };

    struct SourceScoreAggregate {
        QString category;
        int totalScore = 0;
        int entryCount = 0;
    };

    struct PlatformPerfAggregate {
        QString platform;
        QString stage;
        double avgElapsed = 0.0;
        double maxElapsed = 0.0;
        int runCount = 0;
        int successCount = 0;
    };

    struct FailureAggregate {
        QString errorType;
        int count = 0;
    };

    QList<ReliabilityAggregate> aggregateReliabilityByPlatform(int searchLimit = 50);
    QList<SourceScoreAggregate> sourceScoreByCategory();
    QList<PlatformPerfAggregate> platformPerformance();
    QList<PlatformPerfAggregate> platformPerformanceByPlatform(const QString &platform);
    QList<FailureAggregate> failureByType(int days = 7);
    QList<EvolutionChange> evolutionChangesByPlatform(const QString &platform, int limit = 20);

    /// 将 search_history 中未关联知识条目的记录回填到 knowledge 表
    int backfillKnowledgeFromHistory();

    /// 导出全部表数据为 JSON（供 AI 分析用）
    QJsonObject exportAllData(const QString &project = {});

    // === 数据保留 ===

    /// 执行数据保留策略（启动时调用）
    void enforceRetentionPolicy();

private:
    void createTables();
    void migrateSchema();
public:
    void seedDefaultConfig();   // 首次启动写入内置配置默认值
private:
    QString m_dbPath;
    QSqlDatabase m_db;
};
