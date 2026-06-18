#pragma once
/**
 * JSON 帧解析器 —— 4 字节大端长度前缀 + JSON 帧编码/解码。
 *
 * 与 Python agent_protocol.py 的 write_frame/read_frame 格式完全对应。
 * - 帧格式：[4B 大端长度][JSON UTF-8][\n]
 * - seq 不连续 → 仅 WARNING，不中断
 * - 不识别的 event / 字段 → 忽略（向前兼容）
 */

#include <QJsonObject>
#include <QByteArray>
#include <QString>
#include <QPair>

class JsonParser {
public:
    /// 最大帧长度（防止恶意 / 损坏数据）
    static constexpr quint32 MAX_FRAME_SIZE = 10 * 1024 * 1024;  // 10 MB

    /// 解码结果：成功返回 (true, json)，失败返回 (false, 错误信息)
    struct DecodeResult {
        bool ok = false;
        QJsonObject json;
        QString error;
    };

    /**
     * 解码单帧：从 QByteArray 中读取 4B 长度前缀 + JSON 数据。
     * @param data  包含完整帧的字节数组（可含尾随换行）
     * @param pos   读取起始位置，成功时更新为下一帧起始位置
     * @return      DecodeResult
     */
    static DecodeResult decode(const QByteArray &data, int &pos);

    /**
     * 编码单帧：QJsonObject → [4B 长度前缀][JSON][\n]。
     * @return QByteArray 完整帧字节（可直接写入 QProcess stdin）
     */
    static QByteArray encode(const QJsonObject &json);

    /**
     * 流式解码器：从累积缓冲区中持续提取完整帧。
     * 用于 QProcess stdout 的不定长数据流。
     *
     * @param buffer  累积缓冲区（会被修改：已解析的帧被移除）
     * @param frames  输出：解析出的帧列表
     * @return 提取的帧数量
     */
    static int decodeStream(QByteArray &buffer, QList<QJsonObject> &frames);

    /**
     * 校验 seq 连续性。
     * @param lastSeq  上次的 seq 值
     * @param currentSeq  当前帧的 seq
     * @return 是否连续
     */
    static bool checkSeq(int lastSeq, int currentSeq);
};
