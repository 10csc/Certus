#pragma once
/**
 * Windows DPAPI 加密 / 解密。
 *
 * 使用 CryptProtectData / CryptUnprotectData 进行用户会话级别的数据保护。
 * 密文经 base64 编码后存入 SQLite，Python Agent 不接触加密 API。
 *
 * 安全边界：
 *   - 加密 / 解密全在 C++ 侧
 *   - API key 明文只在内存中短暂存在
 *   - 重装系统 / 换密码 / 迁移电脑后解密失败 → 提示用户重新输入
 */

#include <QString>
#include <QByteArray>

class Crypto {
public:
    /**
     * 加密明文 → base64 密文。
     * @param plaintext  原始明文（如 API key）
     * @return base64 编码的 DPAPI 密文，失败返回空字符串
     */
    static QString encrypt(const QString &plaintext);

    /**
     * 解密 base64 密文 → 明文。
     * @param cipherB64  base64 编码的 DPAPI 密文
     * @return 明文，失败返回空字符串（可能 Windows 账户已变更）
     */
    static QString decrypt(const QString &cipherB64);

    /**
     * 计算 SHA-256 前 8 位十六进制（用于校验哈希）。
     * @param data  输入数据
     * @return 16 字符十六进制字符串
     */
    static QString sha256Prefix8(const QString &data);
};
