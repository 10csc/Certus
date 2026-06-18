#pragma once
/**
 * 辅助修复页 —— 接入 Claude / Codex / DeepSeek API 进行代码诊断与修复。
 *
 * 功能：
 *   - 多模型选择（Claude Opus/Sonnet/Haiku、Codex、DeepSeek）
 *   - 文件上下文注入（读取项目文件作为诊断依据）
 *   - 一键应用修复（将 AI 生成的 patch 写入文件）
 *   - 聊天式交互，Markdown 渲染回复
 */
#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class Database;

class RepairPage : public QWidget {
    Q_OBJECT
public:
    explicit RepairPage(QWidget *parent = nullptr);
    void setDatabase(Database *db);

private slots:
    void onSend();
    void onReplyFinished(QNetworkReply *reply);
    void onSelectFiles();
    void onApplyFix();

private:
    void setupUi();
    void appendUserMessage(const QString &text);
    void appendAiMessage(const QString &html);
    void appendSystemMessage(const QString &html);
    QString buildSystemPrompt() const;
    QJsonObject buildApiRequest(const QString &provider, const QString &model,
                                const QString &apiKey, const QString &userText) const;
    void scrollToBottom();

    QTextEdit *m_chatView = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_fileBtn = nullptr;
    QPushButton *m_applyBtn = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    Database *m_db = nullptr;

    QStringList m_contextFiles;     // 用户选择的上下文文件路径
    QString m_lastAiContent;        // 最近一次 AI 回复（用于应用修复）
    QString m_fileContentCache;     // 缓存已读取的文件内容
};
