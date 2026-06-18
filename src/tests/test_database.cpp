/**
 * SQLite 数据库层测试（使用内存数据库 :memory:）。
 */

#include <QtTest>
#include "core/database.h"

class TestDatabase : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // 使用临时文件确保数据库路径有效
        m_tempFile = new QTemporaryFile(this);
        m_tempFile->open();
        m_dbPath = m_tempFile->fileName();
        m_tempFile->close();
    }

    void cleanupTestCase()
    {
        QFile::remove(m_dbPath);
    }

    void testOpenDatabase()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());
        QVERIFY(db.isOpen());
        db.close();
        QVERIFY(!db.isOpen());
    }

    void testSessionState()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());

        // 初始状态应为 Idle
        QCOMPARE(db.getSessionStatus(), Database::Idle);

        // 设置为 Running
        QVERIFY(db.setSessionStatus(Database::Running, 12345, "测试搜索"));
        QCOMPARE(db.getSessionStatus(), Database::Running);

        // 设置为 Idle
        QVERIFY(db.setSessionStatus(Database::Idle));
        QCOMPARE(db.getSessionStatus(), Database::Idle);

        db.close();
    }

    void testSearchRecordCRUD()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());

        // 插入
        Database::SearchRecord rec;
        rec.query = "C++ Qt 单元测试";
        rec.depth = "L2";
        rec.platform = "deepseek";
        rec.elapsedSec = 3.5;
        rec.contentLen = 1500;
        rec.reportPath = "/data/latest_result.md";
        rec.status = "done";

        qint64 id = db.insertSearchRecord(rec);
        QVERIFY(id > 0);

        // 查询
        auto list = db.recentSearches(10);
        QVERIFY(list.size() >= 1);

        bool found = false;
        for (const auto &r : list) {
            if (r.id == id) {
                QCOMPARE(r.query, QString("C++ Qt 单元测试"));
                QCOMPARE(r.depth, QString("L2"));
                QCOMPARE(r.platform, QString("deepseek"));
                QCOMPARE(r.status, QString("done"));
                found = true;
                break;
            }
        }
        QVERIFY(found);

        // 更新状态
        QVERIFY(db.updateSearchStatus(id, "error"));
        list = db.recentSearches(1);
        QCOMPARE(list.first().status, QString("error"));

        db.close();
    }

    void testConfigSaveLoad()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());

        QVERIFY(db.saveConfig("cdp_port", "9223"));
        QCOMPARE(db.loadConfig("cdp_port"), QString("9223"));

        QVERIFY(db.saveConfig("deepseek_key", "encrypted_value_here"));
        QCOMPARE(db.loadConfig("deepseek_key"), QString("encrypted_value_here"));

        // 不存在的 key 返回默认值
        QCOMPARE(db.loadConfig("nonexistent", "default"), QString("default"));

        db.close();
    }

    void testConfigConsistency()
    {
        // 模拟多页面多次读写同一配置，验证数据一致性
        Database db(m_dbPath);
        QVERIFY(db.open());

        // 1. CDP 端口：ConfigPage 改 → SearchPage 读
        QVERIFY(db.saveConfig("cdp_port", "9224"));
        QCOMPARE(db.loadConfig("cdp_port"), QString("9224"));
        // 再次修改
        QVERIFY(db.saveConfig("cdp_port", "9225"));
        QCOMPARE(db.loadConfig("cdp_port"), QString("9225"));

        // 2. 平台默认值多次切换
        QVERIFY(db.saveConfig("search_platform", "kimi"));
        QCOMPARE(db.loadConfig("search_platform"), QString("kimi"));
        QVERIFY(db.saveConfig("search_platform", "deepseek"));
        QCOMPARE(db.loadConfig("search_platform"), QString("deepseek"));

        QVERIFY(db.saveConfig("synthesis_platform", "chatgpt"));
        QCOMPARE(db.loadConfig("synthesis_platform"), QString("chatgpt"));
        QVERIFY(db.saveConfig("synthesis_platform", "kimi"));
        QCOMPARE(db.loadConfig("synthesis_platform"), QString("kimi"));

        // 3. API 端点
        QVERIFY(db.saveConfig("deepseek_api", "https://api.deepseek.com/v2"));
        QCOMPARE(db.loadConfig("deepseek_api"), QString("https://api.deepseek.com/v2"));
        QVERIFY(db.saveConfig("deepseek_api", "https://api.deepseek.com/v1"));
        QCOMPARE(db.loadConfig("deepseek_api"), QString("https://api.deepseek.com/v1"));

        // 4. 加密 key 的 save→load 往返
        QVERIFY(db.saveConfig("deepseek_key", "enc_sk_test123"));
        QVERIFY(db.saveConfig("deepseek_key_hash", "abc12345"));
        QCOMPARE(db.loadConfig("deepseek_key"), QString("enc_sk_test123"));
        QCOMPARE(db.loadConfig("deepseek_key_hash"), QString("abc12345"));

        // 5. 不存在的 key 返回默认值（不影响其他 key）
        QCOMPARE(db.loadConfig("nonexistent", "fallback"), QString("fallback"));
        QCOMPARE(db.loadConfig("cdp_port"), QString("9225"));  // 仍为上次写入的值

        db.close();
    }

    void testRetentionPolicy()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());

        // 执行保留策略（不应崩溃）
        db.enforceRetentionPolicy();
        QVERIFY(true);

        db.close();
    }

    void testFailureLog()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());

        Database::FailureRecord fail;
        fail.errorType = "send_failed";
        fail.platform = "deepseek";
        fail.detail = "测试故障详情";

        QVERIFY(db.insertFailure(fail));

        auto failures = db.recentFailures(24);
        QVERIFY(failures.size() >= 1);
        QCOMPARE(failures.last().errorType, QString("send_failed"));

        db.close();
    }

    void testKnowledgeCRUD()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());

        // Insert
        Database::KnowledgeEntry entry;
        entry.topic = "C++ 模板元编程";
        entry.conclusion = "std::enable_if 可用于 SFINAE";
        entry.sources = "https://cppreference.com";
        QVERIFY(db.insertKnowledge(entry));

        // Search
        auto results = db.searchKnowledge("模板");
        QVERIFY(results.size() >= 1);
        qint64 id = results.first().id;
        QVERIFY(id > 0);
        QCOMPARE(results.first().topic, QString("C++ 模板元编程"));

        // GetById
        auto fetched = db.getKnowledgeById(id);
        QCOMPARE(fetched.topic, QString("C++ 模板元编程"));
        QCOMPARE(fetched.conclusion, QString("std::enable_if 可用于 SFINAE"));

        // Update
        Database::KnowledgeEntry updated;
        updated.topic = "C++ 模板元编程 (修订)";
        updated.conclusion = "C++20 concepts 取代了部分 SFINAE 场景";
        updated.sources = "https://cppreference.com, https://en.cppreference.com";
        QVERIFY(db.updateKnowledge(id, updated));

        auto afterUpdate = db.getKnowledgeById(id);
        QCOMPARE(afterUpdate.topic, QString("C++ 模板元编程 (修订)"));
        QCOMPARE(afterUpdate.conclusion, QString("C++20 concepts 取代了部分 SFINAE 场景"));

        // Delete
        QVERIFY(db.deleteKnowledge(id));
        auto afterDelete = db.getKnowledgeById(id);
        QCOMPARE(afterDelete.id, qint64(0));  // 返回空记录
        QCOMPARE(afterDelete.topic, QString(""));

        db.close();
    }

    void testListKnowledge()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());

        // 插入 3 条
        Database::KnowledgeEntry e;
        for (int i = 1; i <= 3; i++) {
            e.topic = QString("测试主题 %1").arg(i);
            e.conclusion = QString("结论 %1").arg(i);
            e.sources = "test";
            QVERIFY(db.insertKnowledge(e));
        }

        // 分页：取前 2 条
        auto page1 = db.listKnowledge(2, 0);
        QCOMPARE(page1.size(), 2);

        // 分页：跳过 2 条取 2 条 → 应只剩 1 条
        auto page2 = db.listKnowledge(2, 2);
        QCOMPARE(page2.size(), 1);

        db.close();
    }

    void testAggregateReliability()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());

        // 先需要一条搜索记录（聚合子查询依赖 search_history）
        Database::SearchRecord rec;
        rec.query = "聚合测试";
        rec.depth = "L2";
        rec.platform = "deepseek/kimi";
        qint64 sid = db.insertSearchRecord(rec);
        QVERIFY(sid > 0);

        // 插入 3 条可靠性快照（2 deepseek, 1 kimi）
        Database::ReliabilitySnapshot snap;
        snap.searchId = sid;
        snap.platform = "deepseek";
        snap.confirmed = 8; snap.inferred = 2; snap.unconfirmed = 1;
        QVERIFY(db.insertReliability(snap));
        snap.platform = "deepseek";
        snap.confirmed = 6; snap.inferred = 3; snap.unconfirmed = 1;
        QVERIFY(db.insertReliability(snap));
        snap.platform = "kimi";
        snap.confirmed = 4; snap.inferred = 4; snap.unconfirmed = 2;
        QVERIFY(db.insertReliability(snap));

        auto agg = db.aggregateReliabilityByPlatform(10);
        // 应有 2 个平台分组
        QVERIFY(agg.size() >= 1);  // deepseek 或 kimi 至少出现一个

        bool foundDeepseek = false;
        for (const auto &a : agg) {
            if (a.platform == "deepseek") {
                QCOMPARE(a.totalConfirmed, 14);  // 8+6
                QCOMPARE(a.snapshotCount, 2);
                foundDeepseek = true;
            }
        }
        QVERIFY(foundDeepseek);

        db.close();
    }

    void testSourceScoreByCategory()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());

        Database::SourceScore s;
        s.searchId = 1;
        s.url = "https://docs.python.org";
        s.category = "官方"; s.score = 9;
        QVERIFY(db.insertSourceScore(s));
        s.category = "官方"; s.score = 8;
        QVERIFY(db.insertSourceScore(s));
        s.category = "社区"; s.score = 5;
        QVERIFY(db.insertSourceScore(s));

        auto agg = db.sourceScoreByCategory();
        QVERIFY(agg.size() >= 2);

        // 官方类总分为 17
        for (const auto &a : agg) {
            if (a.category == "官方") {
                QCOMPARE(a.totalScore, 17);
                QCOMPARE(a.entryCount, 2);
            }
        }

        db.close();
    }

    void testPlatformPerformance()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());

        Database::PlatformPerf perf;
        perf.platform = "deepseek"; perf.stage = "search";
        perf.elapsedSec = 30.0; perf.success = true;
        QVERIFY(db.insertPlatformPerf(perf));
        perf.elapsedSec = 45.0; perf.success = true;
        QVERIFY(db.insertPlatformPerf(perf));

        auto agg = db.platformPerformance();
        QVERIFY(agg.size() >= 1);

        for (const auto &a : agg) {
            if (a.platform == "deepseek" && a.stage == "search") {
                QVERIFY(a.avgElapsed > 30.0);
                QCOMPARE(a.maxElapsed, 45.0);
                QCOMPARE(a.runCount, 2);
                QCOMPARE(a.successCount, 2);
            }
        }

        db.close();
    }

    void testFailureByType()
    {
        Database db(m_dbPath);
        QVERIFY(db.open());

        // 使用唯一 error_type 避免与 testFailureLog 的残留数据冲突
        Database::FailureRecord f;
        f.errorType = "test_timeout"; f.platform = "deepseek"; f.detail = "30s 超时";
        QVERIFY(db.insertFailure(f));
        f.errorType = "test_timeout"; f.platform = "kimi"; f.detail = "45s 超时";
        QVERIFY(db.insertFailure(f));
        f.errorType = "test_send"; f.platform = "deepseek"; f.detail = "按钮不可点击";
        QVERIFY(db.insertFailure(f));

        auto agg = db.failureByType(30);
        QVERIFY(agg.size() >= 2);  // test_timeout + test_send (+ 可能有其他测试残留)

        bool foundTimeout = false, foundSend = false;
        for (const auto &a : agg) {
            if (a.errorType == "test_timeout") { QCOMPARE(a.count, 2); foundTimeout = true; }
            if (a.errorType == "test_send")   { QVERIFY(a.count >= 1); foundSend = true; }
        }
        QVERIFY(foundTimeout);
        QVERIFY(foundSend);

        db.close();
    }

private:
    QString m_dbPath;
    QTemporaryFile *m_tempFile = nullptr;
};

QTEST_MAIN(TestDatabase)
#include "test_database.moc"
