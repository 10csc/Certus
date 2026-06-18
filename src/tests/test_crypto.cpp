/**
 * DPAPI 加密 / 解密测试。
 */

#include <QtTest>
#include "utils/crypto.h"

class TestCrypto : public QObject {
    Q_OBJECT

private slots:
    void testEncryptDecryptRoundtrip()
    {
        QString original = "sk-test-api-key-1234567890abcdef";
        QString cipher = Crypto::encrypt(original);
        QVERIFY(!cipher.isEmpty());
        QVERIFY(cipher != original);  // 应该不是明文

        QString decrypted = Crypto::decrypt(cipher);
        QCOMPARE(decrypted, original);
    }

    void testDecryptInvalidData()
    {
        QString result = Crypto::decrypt("invalid-base64!!!");
        // 无效密文返回空（非致命错误）
        QVERIFY(result.isEmpty());
    }

    void testEncryptEmptyString()
    {
        QString result = Crypto::encrypt("");
        QVERIFY(result.isEmpty());
    }

    void testDecryptEmptyString()
    {
        QString result = Crypto::decrypt("");
        QVERIFY(result.isEmpty());
    }

    void testSha256Prefix()
    {
        QString hash1 = Crypto::sha256Prefix8("test-key");
        QString hash2 = Crypto::sha256Prefix8("test-key");
        QCOMPARE(hash1, hash2);          // 相同输入 → 相同哈希
        QCOMPARE(hash1.size(), 8);       // 8 十六进制字符 = 前 4 字节
        QVERIFY(hash1 != Crypto::sha256Prefix8("different-key"));
    }
};

QTEST_MAIN(TestCrypto)
#include "test_crypto.moc"
