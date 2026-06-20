#include "database.h"
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

static const QString CONNECTION_NAME = "certus_db";

// ============================================================
// 生命周期
// ============================================================

Database::Database(const QString &dbPath)
    : m_dbPath(dbPath)
{
}

Database::~Database()
{
    close();
}

bool Database::open()
{
    // 确保目录存在
    QFileInfo fi(m_dbPath);
    QDir().mkpath(fi.absolutePath());

    m_db = QSqlDatabase::addDatabase("QSQLITE", CONNECTION_NAME);
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qCritical() << "[Database] 无法打开数据库:" << m_db.lastError().text();
        return false;
    }

    // 启用 WAL 模式
    {
        QSqlQuery q(m_db);
        q.exec("PRAGMA journal_mode=WAL");
        q.exec("PRAGMA foreign_keys=OFF");  // 应用层 JOIN，不用外键约束
    }

    createTables();
    migrateSchema();
    enforceRetentionPolicy();
    backfillKnowledgeFromHistory();
    seedDefaultConfig();

    qInfo() << "[Database] 已打开:" << m_dbPath << "| WAL 模式";
    return true;
}

void Database::seedDefaultConfig()
{
    // 首次启动时写入内置默认值（不覆盖已有配置）
    if (loadConfig("platform_urls", "").isEmpty()) {
        QJsonObject urls;
        urls["deepseek"] = "https://chat.deepseek.com/";
        QJsonDocument doc(urls);
        saveConfig("platform_urls", QString::fromUtf8(doc.toJson()));
    }
    if (loadConfig("chat_path_patterns", "").isEmpty()) {
        QJsonObject patterns;
        {
            QJsonArray arr; arr.append(QString("/a/chat/s/"));
            patterns["deepseek"] = arr;
        }
        QJsonDocument doc(patterns);
        saveConfig("chat_path_patterns", QString::fromUtf8(doc.toJson()));
    }
    if (loadConfig("cdp_port", "").isEmpty()) {
        saveConfig("cdp_port", "9223");
    }
    if (loadConfig("search_platform", "").isEmpty()) {
        saveConfig("search_platform", "deepseek");
    }
    if (loadConfig("synthesis_platform", "").isEmpty()) {
        saveConfig("synthesis_platform", "deepseek");
    }
    if (loadConfig("auto_depth", "").isEmpty()) {
        saveConfig("auto_depth", "1");
    }
    if (loadConfig("auto_launch_browser", "").isEmpty()) {
        saveConfig("auto_launch_browser", "0");
    }
}

void Database::close()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    if (QSqlDatabase::contains(CONNECTION_NAME)) {
        QSqlDatabase::removeDatabase(CONNECTION_NAME);
    }
}

bool Database::isOpen() const
{
    return m_db.isOpen();
}

// ============================================================
// 建表
// ============================================================

void Database::createTables()
{
    QSqlQuery q(m_db);

    // session_state —— 永远只有一行 (id=1)
    q.exec(
        "CREATE TABLE IF NOT EXISTS session_state ("
        "  id INTEGER PRIMARY KEY CHECK (id = 1),"
        "  status TEXT NOT NULL DEFAULT 'idle',"
        "  started_at TEXT,"
        "  pid INTEGER,"
        "  search_query TEXT,"
        "  updated_at TEXT NOT NULL DEFAULT (datetime('now','localtime'))"
        ")");

    // 确保有一行
    q.exec(
        "INSERT OR IGNORE INTO session_state (id, status) VALUES (1, 'idle')");

    // search_history
    q.exec(
        "CREATE TABLE IF NOT EXISTS search_history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  query TEXT,"
        "  depth TEXT,"
        "  platform TEXT,"
        "  project TEXT DEFAULT '',"
        "  started_at TEXT,"
        "  elapsed_sec REAL,"
        "  content_len INTEGER,"
        "  report_path TEXT,"
        "  status TEXT DEFAULT 'done'"
        ")");

    // reliability_snapshot
    q.exec(
        "CREATE TABLE IF NOT EXISTS reliability_snapshot ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  search_id INTEGER,"
        "  platform TEXT,"
        "  confirmed INTEGER DEFAULT 0,"
        "  inferred INTEGER DEFAULT 0,"
        "  unconfirmed INTEGER DEFAULT 0,"
        "  recorded_at TEXT DEFAULT (datetime('now','localtime'))"
        ")");

    // source_score
    q.exec(
        "CREATE TABLE IF NOT EXISTS source_score ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  search_id INTEGER,"
        "  url TEXT,"
        "  category TEXT,"
        "  score INTEGER DEFAULT 0,"
        "  recorded_at TEXT DEFAULT (datetime('now','localtime'))"
        ")");

    // platform_perf
    q.exec(
        "CREATE TABLE IF NOT EXISTS platform_perf ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  platform TEXT,"
        "  stage TEXT,"
        "  elapsed_sec REAL,"
        "  success INTEGER DEFAULT 1,"
        "  recorded_at TEXT DEFAULT (datetime('now','localtime'))"
        ")");

    // evolution_state
    q.exec(
        "CREATE TABLE IF NOT EXISTS evolution_state ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  platform TEXT,"
        "  key TEXT,"
        "  old_value TEXT,"
        "  new_value TEXT,"
        "  changed_at TEXT DEFAULT (datetime('now','localtime'))"
        ")");

    // failure_log
    q.exec(
        "CREATE TABLE IF NOT EXISTS failure_log ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  error_type TEXT,"
        "  platform TEXT,"
        "  detail TEXT,"
        "  recorded_at TEXT DEFAULT (datetime('now','localtime'))"
        ")");

    // config
    q.exec(
        "CREATE TABLE IF NOT EXISTS config ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT,"
        "  updated_at TEXT DEFAULT (datetime('now','localtime'))"
        ")");

    // knowledge
    q.exec(
        "CREATE TABLE IF NOT EXISTS knowledge ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  topic TEXT,"
        "  conclusion TEXT,"
        "  sources TEXT,"
        "  created_at TEXT DEFAULT (datetime('now','localtime'))"
        ")");
}

void Database::migrateSchema()
{
    QSqlQuery q(m_db);

    // 读取当前 schema 版本
    q.exec("PRAGMA user_version");
    int version = q.next() ? q.value(0).toInt() : 0;

    // version 0 → 1: 添加 project 列
    if (version < 1) {
        q.exec("ALTER TABLE search_history ADD COLUMN project TEXT DEFAULT ''");
        version = 1;
    }

    // version 1 → 2: knowledge 表添加 deleted 软删除标记
    if (version < 2) {
        q.exec("ALTER TABLE knowledge ADD COLUMN deleted INTEGER DEFAULT 0");
        version = 2;
    }

    // 更新 schema 版本
    q.exec(QString("PRAGMA user_version = %1").arg(version));
}

// ============================================================
// session_state
// ============================================================

Database::SessionStatus Database::getSessionStatus()
{
    QSqlQuery q(m_db);
    q.exec("SELECT status FROM session_state WHERE id = 1");
    if (q.next()) {
        QString s = q.value(0).toString();
        if (s == "running") return Running;
        if (s == "evolving") return Evolving;
    }
    return Idle;
}

bool Database::setSessionStatus(SessionStatus status, int pid,
                                const QString &searchQuery)
{
    QString statusStr;
    switch (status) {
    case Idle:     statusStr = "idle";     break;
    case Running:  statusStr = "running";  break;
    case Evolving: statusStr = "evolving"; break;
    }

    QSqlQuery q(m_db);
    q.prepare(
        "UPDATE session_state SET status = ?, started_at = datetime('now','localtime'),"
        "  pid = ?, search_query = ?,"
        "  updated_at = datetime('now','localtime')"
        "  WHERE id = 1");
    q.addBindValue(statusStr);
    q.addBindValue(pid);
    q.addBindValue(searchQuery);
    return q.exec();
}

// ============================================================
// search_history
// ============================================================

qint64 Database::insertSearchRecord(const SearchRecord &rec)
{
    QSqlQuery q(m_db);
    q.prepare(
        "INSERT INTO search_history (query, depth, platform, project, started_at,"
        "  elapsed_sec, content_len, report_path, status)"
        "  VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(rec.query);
    q.addBindValue(rec.depth);
    q.addBindValue(rec.platform);
    q.addBindValue(rec.project);
    q.addBindValue(rec.startedAt.isEmpty()
                       ? QDateTime::currentDateTime().toString(Qt::ISODate)
                       : rec.startedAt);
    q.addBindValue(rec.elapsedSec);
    q.addBindValue(rec.contentLen);
    q.addBindValue(rec.reportPath);
    q.addBindValue(rec.status.isEmpty() ? "done" : rec.status);

    if (q.exec()) {
        return q.lastInsertId().toLongLong();
    }
    qWarning() << "[Database] 插入搜索记录失败:" << q.lastError().text();
    return -1;
}

bool Database::updateSearchStatus(qint64 id, const QString &status)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE search_history SET status = ? WHERE id = ?");
    q.addBindValue(status);
    q.addBindValue(id);
    return q.exec();
}

bool Database::updateSearchDetails(qint64 id, double elapsedSec, int contentLen,
                                   const QString &reportPath)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE search_history SET elapsed_sec = ?,"
              "  content_len = ?, report_path = ? WHERE id = ?");
    q.addBindValue(elapsedSec);
    q.addBindValue(contentLen);
    q.addBindValue(reportPath);
    q.addBindValue(id);
    return q.exec();
}

QList<Database::SearchRecord> Database::recentSearches(int limit,
                                                       const QString &filter,
                                                       const QString &project)
{
    QList<SearchRecord> results;
    QSqlQuery q(m_db);

    QString sql = "SELECT id, query, depth, platform, project, started_at,"
                  "  elapsed_sec, content_len, report_path, status"
                  "  FROM search_history WHERE 1=1";
    QList<QVariant> binds;

    if (!filter.isEmpty()) {
        sql += " AND query LIKE ?";
        binds.append("%" + filter + "%");
    }
    if (!project.isEmpty()) {
        sql += " AND project = ?";
        binds.append(project);
    }
    sql += " ORDER BY id DESC LIMIT ?";
    binds.append(limit);

    q.prepare(sql);
    for (const auto &b : binds)
        q.addBindValue(b);

    q.exec();
    while (q.next()) {
        SearchRecord r;
        int col = 0;
        r.id = q.value(col++).toLongLong();
        r.query = q.value(col++).toString();
        r.depth = q.value(col++).toString();
        r.platform = q.value(col++).toString();
        r.project = q.value(col++).toString();
        r.startedAt = q.value(col++).toString();
        r.elapsedSec = q.value(col++).toDouble();
        r.contentLen = q.value(col++).toInt();
        r.reportPath = q.value(col++).toString();
        r.status = q.value(col++).toString();
        results.append(r);
    }
    return results;
}

bool Database::deleteSearchRecord(qint64 id)
{
    if (!m_db.isOpen()) return false;

    // 级联删除关联的可靠性快照和来源评分
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM reliability_snapshot WHERE search_id = ?");
    q.addBindValue(id);
    q.exec();

    q.prepare("DELETE FROM source_score WHERE search_id = ?");
    q.addBindValue(id);
    q.exec();

    q.prepare("DELETE FROM search_history WHERE id = ?");
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

// ============================================================
// reliability_snapshot
// ============================================================

bool Database::insertReliability(const ReliabilitySnapshot &snap)
{
    QSqlQuery q(m_db);
    q.prepare(
        "INSERT INTO reliability_snapshot (search_id, platform, confirmed,"
        "  inferred, unconfirmed) VALUES (?, ?, ?, ?, ?)");
    q.addBindValue(snap.searchId);
    q.addBindValue(snap.platform);
    q.addBindValue(snap.confirmed);
    q.addBindValue(snap.inferred);
    q.addBindValue(snap.unconfirmed);
    return q.exec();
}

// ============================================================
// source_score
// ============================================================

bool Database::insertSourceScore(const SourceScore &score)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO source_score (search_id, url, category, score)"
              "  VALUES (?, ?, ?, ?)");
    q.addBindValue(score.searchId);
    q.addBindValue(score.url);
    q.addBindValue(score.category);
    q.addBindValue(score.score);
    return q.exec();
}

// ============================================================
// platform_perf
// ============================================================

bool Database::insertPlatformPerf(const PlatformPerf &perf)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO platform_perf (platform, stage, elapsed_sec, success)"
              "  VALUES (?, ?, ?, ?)");
    q.addBindValue(perf.platform);
    q.addBindValue(perf.stage);
    q.addBindValue(perf.elapsedSec);
    q.addBindValue(perf.success ? 1 : 0);
    return q.exec();
}

// ============================================================
// evolution_state
// ============================================================

bool Database::insertEvolutionChange(const EvolutionChange &change)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO evolution_state (platform, key, old_value, new_value)"
              "  VALUES (?, ?, ?, ?)");
    q.addBindValue(change.platform);
    q.addBindValue(change.key);
    q.addBindValue(change.oldValue);
    q.addBindValue(change.newValue);
    return q.exec();
}

QList<Database::EvolutionChange> Database::recentEvolutionChanges(int limit)
{
    QList<EvolutionChange> results;
    QSqlQuery q(m_db);
    q.prepare("SELECT platform, key, old_value, new_value"
              "  FROM evolution_state ORDER BY id DESC LIMIT ?");
    q.addBindValue(limit);
    q.exec();
    while (q.next()) {
        EvolutionChange c;
        c.platform = q.value(0).toString();
        c.key = q.value(1).toString();
        c.oldValue = q.value(2).toString();
        c.newValue = q.value(3).toString();
        results.append(c);
    }
    return results;
}

// ============================================================
// failure_log
// ============================================================

bool Database::insertFailure(const FailureRecord &fail)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO failure_log (error_type, platform, detail)"
              "  VALUES (?, ?, ?)");
    q.addBindValue(fail.errorType);
    q.addBindValue(fail.platform);
    q.addBindValue(fail.detail);
    return q.exec();
}

QList<Database::FailureRecord> Database::recentFailures(int hours)
{
    QList<FailureRecord> results;
    QSqlQuery q(m_db);
    q.prepare(
        "SELECT error_type, platform, detail"
        "  FROM failure_log"
        "  WHERE recorded_at >= datetime('now','localtime',?)"
        "  ORDER BY id DESC LIMIT 50");
    q.addBindValue(QString("-%1 hours").arg(hours));
    q.exec();
    while (q.next()) {
        FailureRecord f;
        f.errorType = q.value(0).toString();
        f.platform = q.value(1).toString();
        f.detail = q.value(2).toString();
        results.append(f);
    }
    return results;
}

// ============================================================
// config
// ============================================================

bool Database::saveConfig(const QString &key, const QString &value)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO config (key, value, updated_at)"
              "  VALUES (?, ?, datetime('now','localtime'))");
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}

QString Database::loadConfig(const QString &key, const QString &defaultValue)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT value FROM config WHERE key = ?");
    q.addBindValue(key);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return defaultValue;
}

// ============================================================
// knowledge
// ============================================================

bool Database::insertKnowledge(const KnowledgeEntry &entry)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO knowledge (topic, conclusion, sources)"
              "  VALUES (?, ?, ?)");
    q.addBindValue(entry.topic);
    q.addBindValue(entry.conclusion);
    q.addBindValue(entry.sources);
    return q.exec();
}

bool Database::insertKnowledgeIfNew(const KnowledgeEntry &entry)
{
    // 去重检查：同 topic 且 24 小时内不重复插入
    QSqlQuery check(m_db);
    check.prepare("SELECT id FROM knowledge WHERE topic = ?"
                  "  AND created_at > datetime('now', '-24 hours') LIMIT 1");
    check.addBindValue(entry.topic);
    if (check.exec() && check.next()) {
        return false;  // 已有，跳过
    }
    return insertKnowledge(entry);
}

QList<Database::KnowledgeEntry> Database::searchKnowledge(const QString &keyword)
{
    QList<KnowledgeEntry> results;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, topic, conclusion, sources, created_at"
              "  FROM knowledge WHERE COALESCE(deleted,0)=0"
              "  AND (topic LIKE ? OR conclusion LIKE ?)"
              "  ORDER BY id DESC LIMIT 50");
    q.addBindValue("%" + keyword + "%");
    q.addBindValue("%" + keyword + "%");
    q.exec();
    while (q.next()) {
        KnowledgeEntry e;
        e.id = q.value(0).toLongLong();
        e.topic = q.value(1).toString();
        e.conclusion = q.value(2).toString();
        e.sources = q.value(3).toString();
        e.createdAt = q.value(4).toString();
        results.append(e);
    }
    return results;
}

Database::KnowledgeEntry Database::getKnowledgeById(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT id, topic, conclusion, sources, created_at"
              "  FROM knowledge WHERE id = ?");
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        KnowledgeEntry e;
        e.id = q.value(0).toLongLong();
        e.topic = q.value(1).toString();
        e.conclusion = q.value(2).toString();
        e.sources = q.value(3).toString();
        e.createdAt = q.value(4).toString();
        return e;
    }
    return KnowledgeEntry{};
}

bool Database::updateKnowledge(qint64 id, const KnowledgeEntry &entry)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE knowledge SET topic = ?, conclusion = ?, sources = ?"
              "  WHERE id = ?");
    q.addBindValue(entry.topic);
    q.addBindValue(entry.conclusion);
    q.addBindValue(entry.sources);
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

bool Database::deleteKnowledge(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE knowledge SET deleted = 1 WHERE id = ?");
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

QList<Database::KnowledgeEntry> Database::listKnowledge(int limit, int offset)
{
    QList<KnowledgeEntry> results;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, topic, conclusion, sources, created_at"
              "  FROM knowledge WHERE COALESCE(deleted,0)=0"
              "  ORDER BY id DESC LIMIT ? OFFSET ?");
    q.addBindValue(limit);
    q.addBindValue(offset);
    q.exec();
    while (q.next()) {
        KnowledgeEntry e;
        e.id = q.value(0).toLongLong();
        e.topic = q.value(1).toString();
        e.conclusion = q.value(2).toString();
        e.sources = q.value(3).toString();
        e.createdAt = q.value(4).toString();
        results.append(e);
    }
    return results;
}

// ============================================================
// 监控聚合查询
// ============================================================

QList<Database::ReliabilityAggregate>
Database::aggregateReliabilityByPlatform(int searchLimit)
{
    QList<ReliabilityAggregate> results;
    QSqlQuery q(m_db);
    q.prepare(
        "SELECT platform,"
        "  SUM(confirmed), SUM(inferred), SUM(unconfirmed), COUNT(*)"
        "  FROM reliability_snapshot"
        "  WHERE search_id IN ("
        "    SELECT id FROM search_history ORDER BY id DESC LIMIT ?)"
        "  GROUP BY platform ORDER BY COUNT(*) DESC");
    q.addBindValue(searchLimit);
    q.exec();
    while (q.next()) {
        ReliabilityAggregate a;
        a.platform = q.value(0).toString();
        a.totalConfirmed = q.value(1).toInt();
        a.totalInferred = q.value(2).toInt();
        a.totalUnconfirmed = q.value(3).toInt();
        a.snapshotCount = q.value(4).toInt();
        results.append(a);
    }
    return results;
}

QList<Database::SourceScoreAggregate> Database::sourceScoreByCategory()
{
    QList<SourceScoreAggregate> results;
    QSqlQuery q(m_db);
    q.exec("SELECT category, SUM(score), COUNT(*)"
           "  FROM source_score"
           "  GROUP BY category ORDER BY SUM(score) DESC");
    while (q.next()) {
        SourceScoreAggregate a;
        a.category = q.value(0).toString();
        a.totalScore = q.value(1).toInt();
        a.entryCount = q.value(2).toInt();
        results.append(a);
    }
    return results;
}

QList<Database::PlatformPerfAggregate> Database::platformPerformance()
{
    QList<PlatformPerfAggregate> results;
    QSqlQuery q(m_db);
    q.exec("SELECT platform, stage,"
           "  AVG(elapsed_sec), MAX(elapsed_sec), COUNT(*), SUM(success)"
           "  FROM platform_perf"
           "  GROUP BY platform, stage"
           "  ORDER BY platform, stage");
    while (q.next()) {
        PlatformPerfAggregate a;
        a.platform = q.value(0).toString();
        a.stage = q.value(1).toString();
        a.avgElapsed = q.value(2).toDouble();
        a.maxElapsed = q.value(3).toDouble();
        a.runCount = q.value(4).toInt();
        a.successCount = q.value(5).toInt();
        results.append(a);
    }
    return results;
}

QList<Database::PlatformPerfAggregate>
Database::platformPerformanceByPlatform(const QString &platform)
{
    QList<PlatformPerfAggregate> results;
    QSqlQuery q(m_db);
    q.prepare("SELECT platform, stage,"
              "  AVG(elapsed_sec), MAX(elapsed_sec), COUNT(*), SUM(success)"
              "  FROM platform_perf WHERE platform = ?"
              "  GROUP BY platform, stage"
              "  ORDER BY stage");
    q.addBindValue(platform);
    q.exec();
    while (q.next()) {
        PlatformPerfAggregate a;
        a.platform = q.value(0).toString();
        a.stage = q.value(1).toString();
        a.avgElapsed = q.value(2).toDouble();
        a.maxElapsed = q.value(3).toDouble();
        a.runCount = q.value(4).toInt();
        a.successCount = q.value(5).toInt();
        results.append(a);
    }
    return results;
}

QList<Database::FailureAggregate> Database::failureByType(int days)
{
    QList<FailureAggregate> results;
    QSqlQuery q(m_db);
    q.prepare("SELECT error_type, COUNT(*)"
              "  FROM failure_log"
              "  WHERE recorded_at >= datetime('now','localtime', ?)"
              "  GROUP BY error_type ORDER BY COUNT(*) DESC");
    q.addBindValue(QString("-%1 days").arg(days));
    q.exec();
    while (q.next()) {
        FailureAggregate a;
        a.errorType = q.value(0).toString();
        a.count = q.value(1).toInt();
        results.append(a);
    }
    return results;
}

QList<Database::EvolutionChange>
Database::evolutionChangesByPlatform(const QString &platform, int limit)
{
    QList<EvolutionChange> results;
    QSqlQuery q(m_db);
    q.prepare("SELECT platform, key, old_value, new_value"
              "  FROM evolution_state WHERE platform = ?"
              "  ORDER BY id DESC LIMIT ?");
    q.addBindValue(platform);
    q.addBindValue(limit);
    q.exec();
    while (q.next()) {
        EvolutionChange c;
        c.platform = q.value(0).toString();
        c.key = q.value(1).toString();
        c.oldValue = q.value(2).toString();
        c.newValue = q.value(3).toString();
        results.append(c);
    }
    return results;
}

int Database::backfillKnowledgeFromHistory()
{
    // 找出 search_history 中 status='done' 且未在 knowledge 中有对应记录的条目
    QSqlQuery q(m_db);
    q.exec(
        "SELECT h.id, h.query, h.platform, h.report_path "
        "FROM search_history h "
        "WHERE h.status = 'done' "
        "AND NOT EXISTS (SELECT 1 FROM knowledge k WHERE k.topic = h.query AND COALESCE(k.deleted,0)=0) "
        "AND NOT EXISTS (SELECT 1 FROM knowledge k WHERE k.topic = h.query AND COALESCE(k.deleted,0)=1) "
        "ORDER BY h.id");
    int count = 0;
    while (q.next()) {
        KnowledgeEntry entry;
        entry.topic = q.value(1).toString();
        entry.sources = q.value(2).toString();
        // 尝试从报告文件读摘要
        QString reportPath = q.value(3).toString();
        if (!reportPath.isEmpty()) {
            QFile reportFile(reportPath);
            if (reportFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString content = QString::fromUtf8(reportFile.readAll());
                reportFile.close();
                entry.conclusion = content.left(500)
                    .remove(QRegularExpression("[#*`~>\\[\\]]"))
                    .simplified();
            }
        }
        if (entry.conclusion.isEmpty())
            entry.conclusion = entry.topic;
        if (insertKnowledge(entry))
            count++;
    }
    if (count > 0)
        qInfo() << "[Database] 回填知识条目:" << count << "条";
    return count;
}

// ============================================================
// 数据保留策略
// ============================================================

void Database::enforceRetentionPolicy()
{
    QSqlQuery q(m_db);

    // search_history + 关联表：保留 1 年
    q.exec(
        "DELETE FROM reliability_snapshot WHERE recorded_at < "
        "  datetime('now','localtime','-1 year')");
    q.exec(
        "DELETE FROM source_score WHERE recorded_at < "
        "  datetime('now','localtime','-1 year')");
    q.exec(
        "DELETE FROM platform_perf WHERE recorded_at < "
        "  datetime('now','localtime','-1 year')");
    q.exec(
        "DELETE FROM search_history WHERE started_at < "
        "  datetime('now','localtime','-1 year')");

    // failure_log：保留 90 天
    q.exec(
        "DELETE FROM failure_log WHERE recorded_at < "
        "  datetime('now','localtime','-90 days')");

    qInfo() << "[Database] 数据保留策略已执行";
}

// ============================================================
// 全量数据导出（JSON，供 AI 分析）
// ============================================================

QJsonObject Database::exportAllData(const QString &project)
{
    QJsonObject result;
    result["export_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    result["project"] = project.isEmpty() ? loadConfig("current_project", "") : project;

    QSqlQuery q(m_db);

    // 辅助：执行查询并返回 QJsonArray
    auto queryAll = [&](const QString &sql, const QStringList &cols) -> QJsonArray {
        QJsonArray arr;
        q.exec(sql);
        while (q.next()) {
            QJsonObject obj;
            for (const auto &c : cols)
                obj[c] = QJsonValue::fromVariant(q.value(cols.indexOf(c)));
            arr.append(obj);
        }
        return arr;
    };

    // search_history（按项目过滤，使用 prepared bind 防注入）
    {
        QSqlQuery hq(m_db);
        if (project.isEmpty()) {
            hq.prepare("SELECT id, query, depth, platform, project, started_at,"
                       "  elapsed_sec, content_len, report_path, status"
                       "  FROM search_history ORDER BY id DESC LIMIT 500");
        } else {
            hq.prepare("SELECT id, query, depth, platform, project, started_at,"
                       "  elapsed_sec, content_len, report_path, status"
                       "  FROM search_history WHERE project = ?"
                       "  ORDER BY id DESC LIMIT 500");
            hq.addBindValue(project);
        }
        hq.exec();
        QJsonArray arr;
        QStringList cols = {"id","query","depth","platform","project","started_at",
                            "elapsed_sec","content_len","report_path","status"};
        while (hq.next()) {
            QJsonObject obj;
            for (int i = 0; i < cols.size(); i++)
                obj[cols[i]] = QJsonValue::fromVariant(hq.value(i));
            arr.append(obj);
        }
        result["search_history"] = arr;
    }

    // reliability_snapshot
    result["reliability"] = queryAll(
        "SELECT id, search_id, platform, confirmed, inferred, unconfirmed,"
        "  recorded_at FROM reliability_snapshot ORDER BY id DESC LIMIT 200",
        {"id","search_id","platform","confirmed","inferred","unconfirmed",
         "recorded_at"});

    // source_score
    result["source_scores"] = queryAll(
        "SELECT id, search_id, url, category, score, recorded_at"
        "  FROM source_score ORDER BY id DESC LIMIT 200",
        {"id","search_id","url","category","score","recorded_at"});

    // platform_perf
    result["platform_performance"] = queryAll(
        "SELECT id, platform, stage, elapsed_sec, success, recorded_at"
        "  FROM platform_perf ORDER BY id DESC LIMIT 200",
        {"id","platform","stage","elapsed_sec","success","recorded_at"});

    // evolution_state
    result["evolution_changes"] = queryAll(
        "SELECT id, platform, key, old_value, new_value, changed_at"
        "  FROM evolution_state ORDER BY id DESC LIMIT 100",
        {"id","platform","key","old_value","new_value","changed_at"});

    // failure_log
    result["failure_log"] = queryAll(
        "SELECT id, error_type, platform, detail, recorded_at"
        "  FROM failure_log ORDER BY id DESC LIMIT 100",
        {"id","error_type","platform","detail","recorded_at"});

    // knowledge
    result["knowledge"] = queryAll(
        "SELECT id, topic, conclusion, sources, created_at"
        "  FROM knowledge ORDER BY id DESC LIMIT 200",
        {"id","topic","conclusion","sources","created_at"});

    return result;
}
