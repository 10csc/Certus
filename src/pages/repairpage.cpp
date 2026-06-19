#include "repairpage.h"
#include "../core/database.h"
#include "../utils/crypto.h"
#include "../ui/theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScrollBar>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QRegularExpression>
#include <QDir>
#include <QLabel>

// 前向声明
static QString renderMarkdown(const QString &md);

// ============================================================

RepairPage::RepairPage(QWidget *parent) : QWidget(parent)
{
    m_network = new QNetworkAccessManager(this);
    connect(m_network, &QNetworkAccessManager::finished,
            this, &RepairPage::onReplyFinished);
    setupUi();
}

void RepairPage::setDatabase(Database *db) { m_db = db; }

void RepairPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    // === 工具栏：模型选择 + 文件选择 + 应用修复 ===
    auto *toolbar = new QHBoxLayout();

    m_modelCombo = new QComboBox(this);
    m_modelCombo->addItem("Claude Opus 4.7",    "claude|claude-opus-4-7");
    m_modelCombo->addItem("Claude Sonnet 4.6",  "claude|claude-sonnet-4-6");
    m_modelCombo->addItem("Claude Haiku 4.5",   "claude|claude-haiku-4-5");
    m_modelCombo->addItem("Codex (OpenAI)",      "codex|gpt-4o");
    m_modelCombo->addItem("DeepSeek V4",         "deepseek|deepseek-chat");
    toolbar->addWidget(new QLabel("模型:", this));
    toolbar->addWidget(m_modelCombo);

    toolbar->addSpacing(12);

    m_fileBtn = new QPushButton("+ 添加文件上下文", this);
    m_fileBtn->setProperty("cssClass", "secondary");
    connect(m_fileBtn, &QPushButton::clicked, this, &RepairPage::onSelectFiles);
    toolbar->addWidget(m_fileBtn);

    m_applyBtn = new QPushButton("应用修复", this);
    m_applyBtn->setProperty("cssClass", "success");
    m_applyBtn->setEnabled(false);
    connect(m_applyBtn, &QPushButton::clicked, this, &RepairPage::onApplyFix);
    toolbar->addWidget(m_applyBtn);

    toolbar->addStretch();
    layout->addLayout(toolbar);

    // === 聊天视图 ===
    m_chatView = new QTextEdit(this);
    m_chatView->setReadOnly(true);
    m_chatView->setHtml(
        "<div style='color:#888; text-align:center; margin-top:60px;'>"
        "<p style='font-size:16px;'>辅助修复 —— 多模型 AI 诊断</p>"
        "<p>选择模型，可选添加代码文件作为上下文，然后输入错误信息</p>"
        "<p style='color:#666;'>支持 Claude (Anthropic) / Codex (OpenAI) / DeepSeek</p>"
        "<p style='color:#555;'>提示: 点击「+ 添加文件上下文」可选中 agent/*.py 或 src/*.cpp 作为诊断参考</p></div>");
    layout->addWidget(m_chatView, 1);

    // === 输入行 ===
    auto *inputRow = new QHBoxLayout();
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText("输入错误信息或问题...");
    connect(m_input, &QLineEdit::returnPressed, this, &RepairPage::onSend);

    m_sendBtn = new QPushButton("发送", this);
    m_sendBtn->setProperty("cssClass", "primary");
    connect(m_sendBtn, &QPushButton::clicked, this, &RepairPage::onSend);

    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_sendBtn);
    layout->addLayout(inputRow);
}

// ============================================================
// 文件选择
// ============================================================

void RepairPage::onSelectFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, "选择代码文件作为上下文",
        QDir::currentPath(),
        "代码文件 (*.py *.cpp *.h *.hpp *.c *.js *.ts *.json *.yaml *.yml *.toml);;所有文件 (*.*)");

    if (files.isEmpty()) return;

    m_contextFiles.clear();
    m_fileContentCache.clear();

    for (const QString &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QTextStream in(&file);
        QString content = in.readAll();
        file.close();

        // 限制单文件 80KB
        if (content.size() > 81920) {
            content = content.left(81920) + "\n... (文件过长，已截断)";
        }

        QFileInfo fi(path);
        m_fileContentCache += QString("\n--- %1 ---\n%2\n").arg(fi.fileName(), content);
        m_contextFiles.append(fi.fileName());
    }

    appendSystemMessage(QString(
        "<span style='color:#81c784;'>✓ 已加载 %1 个文件作为上下文: %2</span>")
        .arg(m_contextFiles.size())
        .arg(m_contextFiles.join(", ")));
}

// ============================================================
// 构建系统提示词
// ============================================================

QString RepairPage::buildSystemPrompt() const
{
    QString prompt = QString(
        "你是一个专业的软件工程师助手，运行在 Certus AI 研究系统中。"
        "你的任务是帮助用户诊断代码问题并生成修复方案。\n\n"

        "## 工作原则\n"
        "- 仔细分析用户提供的错误信息和代码上下文\n"
        "- 定位根本原因而非表面现象\n"
        "- 给出具体可操作的修复方案（包含代码 diff 或完整函数）\n"
        "- 用中文回答，专业术语保留英文\n\n"

        "## Certus 项目背景\n"
        "- C++ Qt 6.5.3 桌面前端 (MSVC 2022, CMake)\n"
        "- Python 3.12 Agent 后端 (Playwright CDP)\n"
        "- 通信: subprocess stdin/stdout JSON 帧协议 (4字节大端长度前缀)\n"
        "- 存储: SQLite WAL 模式\n"
        "- 加密: Windows DPAPI (CryptProtectData/CryptUnprotectData)\n"
        "- 平台交互: Playwright 操控浏览器 AI 对话平台 (DeepSeek/Kimi/ChatGPT/Gemini)\n\n"

        "## 修复输出格式\n"
        "当你可以给出具体代码修复时，请使用以下格式，以便用户点击「应用修复」自动应用:\n"
        "```fix\n"
        "文件: path/to/file.cpp\n"
        "查找: <原始代码>\n"
        "替换为: <修复后代码>\n"
        "```\n"
        "注意: 只输出你有把握的修复，不确定的地方请说明假设条件。");

    // 附加文件上下文
    if (!m_fileContentCache.isEmpty()) {
        prompt += "\n\n## 用户提供的代码文件上下文\n" + m_fileContentCache;
    }

    return prompt;
}

// ============================================================
// 构建 API 请求
// ============================================================

QJsonObject RepairPage::buildApiRequest(const QString &provider,
                                         const QString &model,
                                         const QString &apiKey,
                                         const QString &userText) const
{
    QString systemPrompt = buildSystemPrompt();

    if (provider == "claude") {
        // Anthropic Messages API
        QJsonObject req;
        req["model"] = model;
        req["max_tokens"] = 4096;
        req["temperature"] = 0.3;

        QJsonArray messages;
        QJsonObject userMsg;
        userMsg["role"] = "user";

        // Claude 不支持 system role 在 messages 中，需要在顶层设置
        req["system"] = systemPrompt;

        QJsonArray content;
        QJsonObject textBlock;
        textBlock["type"] = "text";
        textBlock["text"] = userText;
        content.append(textBlock);
        userMsg["content"] = content;
        messages.append(userMsg);
        req["messages"] = messages;

        return req;
    }

    // OpenAI 兼容 API (Codex / DeepSeek)
    QJsonObject req;
    req["model"] = model;
    req["temperature"] = 0.3;
    req["max_tokens"] = 4096;

    QJsonArray messages;
    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    messages.append(sysMsg);

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = userText;
    messages.append(userMsg);
    req["messages"] = messages;

    return req;
}

// ============================================================
// 发送消息
// ============================================================

void RepairPage::onSend()
{
    QString text = m_input->text().trimmed();
    if (text.isEmpty() || !m_db) return;

    appendUserMessage(text);
    m_input->clear();
    m_sendBtn->setEnabled(false);
    m_sendBtn->setText("等待中...");

    // 解析模型选择
    QStringList parts = m_modelCombo->currentData().toString().split('|');
    QString provider = parts.value(0, "deepseek");
    QString model = parts.value(1, "deepseek-chat");

    // 根据 provider 读取对应的 API 配置
    QString apiUrl;
    QString apiKey;

    if (provider == "claude") {
        apiUrl = "https://api.anthropic.com/v1/messages";
        apiKey = m_db->loadConfig("claude_key", "");
    } else if (provider == "codex") {
        apiUrl = m_db->loadConfig("codex_api", "https://api.openai.com/v1") + "/chat/completions";
        apiKey = m_db->loadConfig("codex_key", "");
    } else {  // deepseek
        apiUrl = m_db->loadConfig("deepseek_api", "https://api.deepseek.com/v1") + "/chat/completions";
        apiKey = m_db->loadConfig("deepseek_key", "");
    }

    // 解密 API Key
    if (!apiKey.isEmpty()) {
        apiKey = Crypto::decrypt(apiKey);
    }

    if (apiKey.isEmpty()) {
        appendSystemMessage(QString(
            "<span style='color:#ff9800;'>⚠ %1 API Key 未配置，请在「配置」页设置</span>")
            .arg(provider == "claude" ? "Claude" : (provider == "codex" ? "Codex" : "DeepSeek")));
        m_sendBtn->setEnabled(true);
        m_sendBtn->setText("发送");
        return;
    }

    // 构建请求
    QJsonObject payload = buildApiRequest(provider, model, apiKey, text);

    QNetworkRequest netReq;
    netReq.setUrl(QUrl(apiUrl));
    netReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    if (provider == "claude") {
        netReq.setRawHeader("x-api-key", apiKey.toUtf8());
        netReq.setRawHeader("anthropic-version", "2023-06-01");
    } else {
        netReq.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
    }

    QByteArray postData = QJsonDocument(payload).toJson();
    m_network->post(netReq, postData);
}

// ============================================================
// 处理回复
// ============================================================

void RepairPage::onReplyFinished(QNetworkReply *reply)
{
    m_sendBtn->setEnabled(true);
    m_sendBtn->setText("发送");

    if (reply->error() != QNetworkReply::NoError) {
        QString errMsg = reply->errorString();
        int statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (statusCode == 401) {
            appendSystemMessage("<span style='color:#f44336;'>✗ API Key 无效 (401)，请检查配置</span>");
        } else if (statusCode == 403) {
            appendSystemMessage("<span style='color:#f44336;'>✗ 访问被拒 (403)，请检查 API 权限</span>");
        } else if (statusCode == 429) {
            appendSystemMessage("<span style='color:#ff9800;'>⚠ 请求过于频繁 (429)，请稍后重试</span>");
        } else if (statusCode >= 500) {
            appendSystemMessage(QString("<span style='color:#f44336;'>✗ API 服务器错误 (%1)</span>").arg(statusCode));
        } else {
            appendSystemMessage(QString("<span style='color:#f44336;'>✗ 网络错误: %1</span>").arg(errMsg));
        }
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject body = doc.object();
    QString content;

    // 根据 API 类型解析回复
    if (body.contains("content")) {
        // Anthropic 格式: content 是数组
        QJsonArray contentArr = body["content"].toArray();
        for (const auto &block : contentArr) {
            QJsonObject b = block.toObject();
            if (b["type"].toString() == "text") {
                content += b["text"].toString();
            }
        }
    } else if (body.contains("choices")) {
        // OpenAI 兼容格式
        QJsonArray choices = body["choices"].toArray();
        if (!choices.isEmpty()) {
            content = choices[0].toObject()["message"].toObject()["content"].toString();
        }
    }

    if (content.isEmpty()) {
        appendSystemMessage("<span style='color:#f44336;'>✗ AI 回复为空</span>");
        return;
    }

    m_lastAiContent = content;
    m_applyBtn->setEnabled(content.contains("```fix") || content.contains("```diff"));

    // Markdown → HTML 渲染
    QString html = renderMarkdown(content);
    appendAiMessage(html);
}

// ============================================================
// 应用修复
// ============================================================

void RepairPage::onApplyFix()
{
    if (m_lastAiContent.isEmpty()) return;

    // 提取 ```fix ... ``` 代码块
    QRegularExpression fixRe(
        "```fix\\s*\\n(.*?)```",
        QRegularExpression::DotMatchesEverythingOption);

    QStringList fixes;
    QRegularExpressionMatchIterator it = fixRe.globalMatch(m_lastAiContent);
    while (it.hasNext()) {
        fixes.append(it.next().captured(1).trimmed());
    }

    if (fixes.isEmpty()) {
        // 尝试 diff 格式
        QRegularExpression diffRe(
            "```diff\\s*\\n(.*?)```",
            QRegularExpression::DotMatchesEverythingOption);
        it = diffRe.globalMatch(m_lastAiContent);
        while (it.hasNext()) {
            fixes.append(it.next().captured(1).trimmed());
        }
    }

    if (fixes.isEmpty()) {
        appendSystemMessage(
            "<span style='color:#ff9800;'>⚠ AI 回复中未检测到可应用的修复块 (```fix 或 ```diff)</span>");
        return;
    }

    int applied = 0;
    QStringList results;

    for (const QString &fixBlock : fixes) {
        // 解析: 文件 / 查找 / 替换为
        QRegularExpression fileRe("文件:\\s*(\\S+)");
        QRegularExpression findRe("查找:\\s*\\n?(.*?)(?:\\n替换为:|$)",
                                   QRegularExpression::DotMatchesEverythingOption);
        QRegularExpression replaceRe("替换为:\\s*\\n?(.*?)$",
                                      QRegularExpression::DotMatchesEverythingOption);

        QString fileName = fileRe.match(fixBlock).captured(1);
        QString findText = findRe.match(fixBlock).captured(1).trimmed();
        QString replaceText = replaceRe.match(fixBlock).captured(1).trimmed();

        if (fileName.isEmpty()) {
            results.append("<span style='color:#ff9800;'>⚠ 修复块缺少文件名，跳过</span>");
            continue;
        }

        // 查找文件
        QString filePath;
        QStringList candidates = {
            QDir::currentPath() + "/agent/" + fileName,
            QDir::currentPath() + "/src/" + fileName,
            QDir::currentPath() + "/" + fileName,
        };
        for (const auto &c : candidates) {
            if (QFile::exists(c)) { filePath = c; break; }
        }

        if (filePath.isEmpty()) {
            results.append(QString(
                "<span style='color:#ff9800;'>⚠ 找不到文件: %1</span>").arg(fileName));
            continue;
        }

        // 读取原文件
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            results.append(QString(
                "<span style='color:#f44336;'>✗ 无法读取: %1</span>").arg(fileName));
            continue;
        }
        QString original = QString::fromUtf8(file.readAll());
        file.close();

        // 执行替换
        QString modified = original;
        if (!findText.isEmpty() && original.contains(findText)) {
            modified.replace(findText, replaceText);
        } else if (!replaceText.isEmpty()) {
            // 没有查找文本 → 追加到文件末尾
            modified += "\n" + replaceText;
        }

        // 备份原文件
        QString backupPath = filePath + ".bak";
        QFile::remove(backupPath);
        QFile::copy(filePath, backupPath);

        // 写入修复
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            results.append(QString(
                "<span style='color:#f44336;'>✗ 无法写入: %1</span>").arg(fileName));
            continue;
        }
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << modified;
        file.close();

        applied++;
        results.append(QString(
            "<span style='color:#81c784;'>✓ 已修复: %1 (备份: %1.bak)</span>").arg(fileName));
    }

    if (applied > 0) {
        appendSystemMessage(QString(
            "<b>应用修复结果:</b><br>%1<br>"
            "<span style='color:#aaa;'>共应用 %2 处修复，原文件已备份为 .bak</span>")
            .arg(results.join("<br>")).arg(applied));
    } else {
        appendSystemMessage(QString(
            "<b>应用修复结果:</b><br>%1").arg(results.join("<br>")));
    }
}

// ============================================================
// 简易 Markdown → HTML 渲染
// ============================================================

static QString renderMarkdown(const QString &md)
{
    QString html;
    html += "<div style='line-height:1.7;'>";

    const QStringList lines = md.split('\n');
    bool inCodeBlock = false;
    QString codeLang;
    QString codeBuf;

    for (const QString &l : lines) {
        if (l.startsWith("```")) {
            if (inCodeBlock) {
                html += "<pre style='background:#0d1117;padding:12px 16px;border-radius:4px;"
                        "border:1px solid #30363d;overflow-x:auto;margin:8px 0;"
                        "font-family:Consolas,monospace;font-size:12px;'>"
                        "<code>" + codeBuf.toHtmlEscaped() + "</code></pre>";
                codeBuf.clear();
                inCodeBlock = false;
            } else {
                inCodeBlock = true;
                codeLang = l.mid(3).trimmed();
            }
            continue;
        }
        if (inCodeBlock) {
            if (!codeBuf.isEmpty()) codeBuf += '\n';
            codeBuf += l;
            continue;
        }

        if (l.startsWith("### ")) {
            html += "<h4 style='color:#d2a8ff;margin:14px 0 4px;'>" + l.mid(4).toHtmlEscaped() + "</h4>";
        } else if (l.startsWith("## ")) {
            html += "<h3 style='color:#7ee787;margin:16px 0 6px;'>" + l.mid(3).toHtmlEscaped() + "</h3>";
        } else if (l.startsWith("# ")) {
            html += "<h2 style='color:#58a6ff;margin:18px 0 8px;'>" + l.mid(2).toHtmlEscaped() + "</h2>";
        } else if (l.startsWith("> ")) {
            html += "<blockquote style='border-left:3px solid #58a6ff;padding:4px 12px;"
                    "margin:6px 0;color:#8b949e;background:#1c2129;'>"
                    + l.mid(2).toHtmlEscaped() + "</blockquote>";
        } else if (l.trimmed().startsWith("- ") || l.trimmed().startsWith("* ")) {
            html += "<li style='margin:2px 0;'>" + l.trimmed().mid(2).toHtmlEscaped() + "</li>";
        } else if (l.trimmed().isEmpty()) {
            html += "<br>";
        } else {
            // 内联格式化
            QString t = l.toHtmlEscaped();
            t.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<strong>\\1</strong>");
            t.replace(QRegularExpression("`(.+?)`"), "<code style='background:#1c2129;color:#f0883e;"
                      "padding:1px 5px;border-radius:3px;font-family:Consolas,monospace;'>\\1</code>");
            html += "<p style='margin:6px 0;'>" + t + "</p>";
        }
    }
    if (!codeBuf.isEmpty()) {
        html += "<pre style='background:#0d1117;padding:12px 16px;border-radius:4px;"
                "border:1px solid #30363d;overflow-x:auto;margin:8px 0;'>"
                "<code>" + codeBuf.toHtmlEscaped() + "</code></pre>";
    }
    html += "</div>";
    return html;
}

// ============================================================
// 消息渲染
// ============================================================

void RepairPage::scrollToBottom()
{
    if (m_chatView) {
        m_chatView->verticalScrollBar()->setValue(
            m_chatView->verticalScrollBar()->maximum());
    }
}

void RepairPage::appendUserMessage(const QString &text)
{
    m_chatView->append(
        QString("<div style='text-align:right; margin:8px 0;'>"
                "<span style='background:%1; color:white; padding:8px 14px; "
                "border-radius:12px 12px 2px 12px; display:inline-block; max-width:80%%; "
                "font-size:13px;'>%2</span></div>")
            .arg(Theme::Accent, text.toHtmlEscaped()));
    scrollToBottom();
}

void RepairPage::appendAiMessage(const QString &html)
{
    m_chatView->append(
        QString("<div style='background:#252525; border-left:3px solid #4caf50; "
                "padding:8px 14px; margin:8px 0; border-radius:0 4px 4px 0;'>"
                "<p style='color:#81c784; margin:0 0 6px 0;'><strong>AI:</strong></p>%1</div>")
            .arg(html));
    scrollToBottom();
}

void RepairPage::appendSystemMessage(const QString &html)
{
    m_chatView->append(
        QString("<p style='color:#888; margin:4px 0;'>%1</p>").arg(html));
    scrollToBottom();
}
