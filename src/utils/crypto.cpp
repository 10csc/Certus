#include "crypto.h"

#include <windows.h>
#include <wincrypt.h>
#include <QCryptographicHash>

// ============================================================
// DPAPI 加密
// ============================================================

QString Crypto::encrypt(const QString &plaintext)
{
    if (plaintext.isEmpty()) return {};

    // 准备输入数据
    QByteArray utf8 = plaintext.toUtf8();
    DATA_BLOB inBlob;
    inBlob.pbData = reinterpret_cast<BYTE *>(utf8.data());
    inBlob.cbData = static_cast<DWORD>(utf8.size());

    DATA_BLOB outBlob;
    outBlob.pbData = nullptr;
    outBlob.cbData = 0;

    // DPAPI 加密（当前用户会话级别）
    if (!CryptProtectData(
            &inBlob,
            L"Certus API Key",          // 描述（可选）
            nullptr,                     // 额外熵（可选）
            nullptr,                     // 保留
            nullptr,                     // 提示结构
            CRYPTPROTECT_UI_FORBIDDEN,  // 不弹 UI
            &outBlob)) {
        return {};
    }

    // base64 编码输出
    QByteArray cipher(
        reinterpret_cast<const char *>(outBlob.pbData),
        static_cast<int>(outBlob.cbData));
    LocalFree(outBlob.pbData);

    return QString::fromUtf8(cipher.toBase64());
}

// ============================================================
// DPAPI 解密
// ============================================================

QString Crypto::decrypt(const QString &cipherB64)
{
    if (cipherB64.isEmpty()) return {};

    // base64 解码
    QByteArray cipher = QByteArray::fromBase64(cipherB64.toUtf8());

    DATA_BLOB inBlob;
    inBlob.pbData = reinterpret_cast<BYTE *>(cipher.data());
    inBlob.cbData = static_cast<DWORD>(cipher.size());

    DATA_BLOB outBlob;
    outBlob.pbData = nullptr;
    outBlob.cbData = 0;

    // DPAPI 解密
    if (!CryptUnprotectData(
            &inBlob,
            nullptr,                     // 描述（输出）
            nullptr,                     // 额外熵
            nullptr,                     // 保留
            nullptr,                     // 提示结构
            CRYPTPROTECT_UI_FORBIDDEN,
            &outBlob)) {
        // 解密失败：可能 Windows 账户已变更 / 重装系统
        return {};
    }

    QString plaintext = QString::fromUtf8(
        reinterpret_cast<const char *>(outBlob.pbData),
        static_cast<int>(outBlob.cbData));
    LocalFree(outBlob.pbData);

    return plaintext;
}

// ============================================================
// SHA-256 校验哈希
// ============================================================

QString Crypto::sha256Prefix8(const QString &data)
{
    QByteArray hash = QCryptographicHash::hash(
        data.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromUtf8(hash.toHex().left(8));
}
