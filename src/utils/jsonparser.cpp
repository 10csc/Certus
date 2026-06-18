#include "jsonparser.h"
#include <QJsonDocument>
#include <QDebug>

// ============================================================
// 解码
// ============================================================

JsonParser::DecodeResult JsonParser::decode(const QByteArray &data, int &pos)
{
    DecodeResult result;

    // 至少需要 4 字节长度前缀
    if (pos + 4 > data.size()) {
        result.error = QString("数据不足：需要 4 字节前缀，实际剩余 %1 字节")
                           .arg(data.size() - pos);
        return result;
    }

    // 读取 4 字节大端长度
    quint32 frameLen = 0;
    const uchar *ptr = reinterpret_cast<const uchar *>(data.constData()) + pos;
    frameLen = (static_cast<quint32>(ptr[0]) << 24)
             | (static_cast<quint32>(ptr[1]) << 16)
             | (static_cast<quint32>(ptr[2]) << 8)
             | static_cast<quint32>(ptr[3]);
    pos += 4;

    // 长度合法性检查
    if (frameLen == 0 || frameLen > MAX_FRAME_SIZE) {
        result.error = QString("非法帧长度: %1 (最大 %2)")
                           .arg(frameLen).arg(MAX_FRAME_SIZE);
        return result;
    }

    // 读取 JSON 数据
    if (pos + static_cast<int>(frameLen) > data.size()) {
        result.error = QString("数据不足：需要 %1 字节 JSON，实际剩余 %2")
                           .arg(frameLen).arg(data.size() - pos);
        return result;
    }

    QByteArray jsonData = data.mid(pos, static_cast<int>(frameLen));
    pos += static_cast<int>(frameLen);

    // 跳过尾随换行（可选）
    if (pos < data.size() && data.at(pos) == '\n') {
        pos++;
    }

    // 解析 JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.error = QString("JSON 解析失败: %1").arg(parseError.errorString());
        return result;
    }

    if (!doc.isObject()) {
        result.error = "JSON 不是对象类型";
        return result;
    }

    result.ok = true;
    result.json = doc.object();
    return result;
}

// ============================================================
// 编码
// ============================================================

QByteArray JsonParser::encode(const QJsonObject &json)
{
    QJsonDocument doc(json);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 4 字节大端长度前缀
    quint32 len = static_cast<quint32>(jsonData.size());
    QByteArray frame;
    frame.resize(4);
    frame[0] = static_cast<char>((len >> 24) & 0xFF);
    frame[1] = static_cast<char>((len >> 16) & 0xFF);
    frame[2] = static_cast<char>((len >> 8) & 0xFF);
    frame[3] = static_cast<char>(len & 0xFF);

    frame.append(jsonData);
    frame.append('\n');
    return frame;
}

// ============================================================
// 流式解码
// ============================================================

int JsonParser::decodeStream(QByteArray &buffer, QList<QJsonObject> &frames)
{
    int count = 0;
    int pos = 0;

    while (pos + 4 <= buffer.size()) {
        int savedPos = pos;
        DecodeResult result = decode(buffer, pos);

        if (result.ok) {
            frames.append(result.json);
            count++;
        } else {
            // 数据不完整 → 保留剩余数据等待更多输入
            pos = savedPos;
            break;
        }
    }

    // 移除已解析的数据
    if (pos > 0) {
        buffer.remove(0, pos);
    }

    return count;
}

// ============================================================
// Seq 校验
// ============================================================

bool JsonParser::checkSeq(int lastSeq, int currentSeq)
{
    if (lastSeq < 0) {
        // 首个帧，不需要校验
        return true;
    }
    if (currentSeq != lastSeq + 1) {
        qWarning() << "[JsonParser] seq 不连续: 期望" << (lastSeq + 1)
                   << "实际" << currentSeq << "(UDP 风格，不中断)";
        return false;
    }
    return true;
}
