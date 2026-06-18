/**
 * JSON 帧解析器测试。
 */

#include <QtTest>
#include <QJsonObject>
#include "utils/jsonparser.h"

class TestJsonParser : public QObject {
    Q_OBJECT

private slots:
    void testEncodeDecodeRoundtrip()
    {
        // 编码 → 解码往返
        QJsonObject original;
        original["event"] = "stage_start";
        original["seq"] = 1;
        original["timestamp"] = 1718200000.123;
        original["stage"] = "search_1";
        original["question"] = "测试?";
        original["platform"] = "deepseek";

        QByteArray frame = JsonParser::encode(original);
        QVERIFY(!frame.isEmpty());

        // 验证长度前缀
        QVERIFY(frame.size() >= 4);
        quint32 len = (static_cast<quint32>(static_cast<uchar>(frame[0])) << 24)
                    | (static_cast<quint32>(static_cast<uchar>(frame[1])) << 16)
                    | (static_cast<quint32>(static_cast<uchar>(frame[2])) << 8)
                    | static_cast<quint32>(static_cast<uchar>(frame[3]));
        QCOMPARE(static_cast<int>(len), frame.size() - 5);  // -4 prefix -1 \n

        // 解码
        int pos = 0;
        auto result = JsonParser::decode(frame, pos);
        QVERIFY2(result.ok, result.error.toUtf8());
        QCOMPARE(result.json["event"].toString(), QString("stage_start"));
        QCOMPARE(result.json["stage"].toString(), QString("search_1"));
        QCOMPARE(result.json["platform"].toString(), QString("deepseek"));
    }

    void testSeqContinuity()
    {
        QVERIFY(JsonParser::checkSeq(-1, 1));   // 首帧
        QVERIFY(JsonParser::checkSeq(1, 2));     // 连续
        QVERIFY(!JsonParser::checkSeq(1, 3));    // 跳号 → 仅 WARNING
        QVERIFY(JsonParser::checkSeq(5, 6));     // 连续
    }

    void testEmptyFrame()
    {
        int pos = 0;
        auto result = JsonParser::decode(QByteArray(), pos);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("数据不足"));
    }

    void testTooLargeFrame()
    {
        // 构造一个声明长度为 100MB 的非法帧
        QByteArray data;
        data.append('\xFF');
        data.append('\xFF');
        data.append('\xFF');
        data.append('\xFF');  // 长度 = 4GB (大于 MAX_FRAME_SIZE)
        data.append("payload");

        int pos = 0;
        auto result = JsonParser::decode(data, pos);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("非法帧长度"));
    }

    void testDecodeStream()
    {
        // 构造两个连续帧
        QJsonObject f1, f2;
        f1["event"] = "stage_start";
        f1["seq"] = 1;
        f2["event"] = "stage_done";
        f2["seq"] = 2;

        QByteArray buffer = JsonParser::encode(f1) + JsonParser::encode(f2);

        QList<QJsonObject> frames;
        int count = JsonParser::decodeStream(buffer, frames);
        QCOMPARE(count, 2);
        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames[0]["event"].toString(), QString("stage_start"));
        QCOMPARE(frames[1]["event"].toString(), QString("stage_done"));

        // 缓冲区应该已清空
        QVERIFY(buffer.isEmpty());
    }

    void testDecodeStreamPartial()
    {
        // 模拟不完整帧：只有半帧数据
        QJsonObject f1;
        f1["event"] = "test";
        QByteArray fullFrame = JsonParser::encode(f1);
        QByteArray partial = fullFrame.left(fullFrame.size() / 2);

        QList<QJsonObject> frames;
        int count = JsonParser::decodeStream(partial, frames);
        QCOMPARE(count, 0);  // 数据不足
        QVERIFY(!partial.isEmpty());  // 剩余数据被保留
    }

    void testForwardCompatibility()
    {
        // 未知字段应被忽略
        QJsonObject withExtra;
        withExtra["event"] = "done";
        withExtra["seq"] = 1;
        withExtra["timestamp"] = 1.0;
        withExtra["unknown_field"] = "should_be_ignored";
        withExtra["future_version_field"] = 42;

        QByteArray frame = JsonParser::encode(withExtra);
        int pos = 0;
        auto result = JsonParser::decode(frame, pos);

        QVERIFY(result.ok);
        QCOMPARE(result.json["event"].toString(), QString("done"));
        // 未知字段存在但不影响解析
        QVERIFY(result.json.contains("unknown_field"));
    }
};

QTEST_MAIN(TestJsonParser)
#include "test_jsonparser.moc"
