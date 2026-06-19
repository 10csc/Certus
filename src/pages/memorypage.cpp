#include "memorypage.h"
#include "../core/database.h"
#include "../ui/theme.h"
#include "../ui/toast.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

MemoryPage::MemoryPage(QWidget *parent) : QWidget(parent) { setupUi(); }

void MemoryPage::setDatabase(Database *db) { m_db = db; }

void MemoryPage::setPythonPath(const QString &path) { m_pythonPath = path; }

void MemoryPage::setAgentDir(const QString &dir) { m_agentDir = dir; }

void MemoryPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);

    // 搜索行
    auto *header = new QHBoxLayout();
    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText("搜索知识库...");
    auto *searchBtn = new QPushButton("搜索", this);
    searchBtn->setProperty("cssClass", "primary");
    connect(searchBtn, &QPushButton::clicked, this, &MemoryPage::onSearch);
    m_semanticBtn = new QPushButton("语义搜索", this);
    m_semanticBtn->setProperty("cssClass", "secondary");
    m_semanticBtn->setToolTip("使用 ChromaDB 语义搜索（需要 Python 环境）");
    connect(m_semanticBtn, &QPushButton::clicked, this, &MemoryPage::onSemanticSearch);
    header->addWidget(m_searchInput, 1);
    header->addWidget(searchBtn);
    header->addWidget(m_semanticBtn);

    // 条目计数
    m_countLabel = new QLabel("共 0 条", this);
    m_countLabel->setStyleSheet(QString("color:%1; font-size:12px; padding:0 4px;").arg(Theme::TextMuted));
    header->addWidget(m_countLabel);

    layout->addLayout(header);

    // 操作按钮行
    auto *btnRow = new QHBoxLayout();
    m_addBtn = new QPushButton("新增", this);
    m_editBtn = new QPushButton("编辑", this);
    m_deleteBtn = new QPushButton("删除", this);
    m_addBtn->setProperty("cssClass", "success");
    m_editBtn->setProperty("cssClass", "secondary");
    m_deleteBtn->setProperty("cssClass", "danger");
    connect(m_addBtn, &QPushButton::clicked, this, &MemoryPage::onAdd);
    connect(m_editBtn, &QPushButton::clicked, this, &MemoryPage::onEdit);
    connect(m_deleteBtn, &QPushButton::clicked, this, &MemoryPage::onDelete);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_editBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    // 列表
    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::itemClicked, this, &MemoryPage::onItemSelected);
    layout->addWidget(m_list, 1);

    // 详情
    m_detailView = new QTextEdit(this);
    m_detailView->setReadOnly(true);
    m_detailView->setMaximumHeight(200);
    layout->addWidget(m_detailView);
}

void MemoryPage::onSearch()
{
    if (!m_db) return;
    m_list->clear();
    m_idCache.clear();
    m_detailView->clear();

    QString kw = m_searchInput->text().trimmed();
    QList<Database::KnowledgeEntry> results;
    if (kw.isEmpty()) {
        results = m_db->listKnowledge(100, 0);
    } else {
        results = m_db->searchKnowledge(kw);
    }

    for (const auto &e : results) {
        m_list->addItem(QString("[%1] %2").arg(e.createdAt.left(10), e.topic));
        m_idCache.append(e.id);
    }
    m_countLabel->setText(QString("共 %1 条").arg(results.size()));
}

void MemoryPage::onItemSelected(QListWidgetItem *)
{
    int row = m_list->currentRow();
    if (row < 0 || row >= m_idCache.size()) return;
    loadDetail(m_idCache[row]);
}

void MemoryPage::loadDetail(qint64 id)
{
    if (!m_db) return;
    auto e = m_db->getKnowledgeById(id);
    if (e.id == 0) {
        m_detailView->setHtml("<p style='color:#888;'>条目不存在</p>");
        return;
    }
    QString html = QString(
        "<h3 style='color:#4fc3f7; margin:0;'>%1</h3>"
        "<p style='margin:8px 0;'><b>结论：</b>%2</p>"
        "<p style='color:#888; margin:4px 0;'><b>来源：</b>%3</p>"
        "<p style='color:#666; margin:4px 0;'><b>时间：</b>%4</p>")
        .arg(e.topic.toHtmlEscaped(),
             e.conclusion.toHtmlEscaped(),
             e.sources.toHtmlEscaped(),
             e.createdAt);
    m_detailView->setHtml(html);
}

void MemoryPage::onAdd()
{
    Database::KnowledgeEntry entry;
    entry.topic = "";
    entry.conclusion = "";
    entry.sources = "";

    // 简单表单对话框
    QDialog dlg(this);
    dlg.setWindowTitle("新增知识条目");
    dlg.setStyleSheet(QString("background:%1; color:%2;").arg(Theme::BgTertiary, Theme::TextPrimary));
    auto *form = new QFormLayout(&dlg);
    auto *topicEdit = new QLineEdit(&dlg);
    auto *conclusionEdit = new QLineEdit(&dlg);
    auto *sourcesEdit = new QLineEdit(&dlg);
    form->addRow("主题:", topicEdit);
    form->addRow("结论:", conclusionEdit);
    form->addRow("来源:", sourcesEdit);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);

    if (dlg.exec() == QDialog::Accepted && m_db) {
        entry.topic = topicEdit->text().trimmed();
        entry.conclusion = conclusionEdit->text().trimmed();
        entry.sources = sourcesEdit->text().trimmed();
        if (!entry.topic.isEmpty()) {
            m_db->insertKnowledge(entry);
            // 同步到 ChromaDB
            auto inserted = m_db->searchKnowledge(entry.topic);
            if (!inserted.isEmpty()) {
                syncKnowledgeToChroma(inserted.first().id, entry.topic,
                                     entry.conclusion, entry.sources,
                                     inserted.first().createdAt);
            }
            refresh();
        }
    }
}

void MemoryPage::onEdit()
{
    int row = m_list->currentRow();
    if (row < 0 || row >= m_idCache.size() || !m_db) return;
    auto entry = m_db->getKnowledgeById(m_idCache[row]);
    if (entry.id == 0) return;

    QDialog dlg(this);
    dlg.setWindowTitle("编辑知识条目");
    dlg.setStyleSheet(QString("background:%1; color:%2;").arg(Theme::BgTertiary, Theme::TextPrimary));
    auto *form = new QFormLayout(&dlg);
    auto *topicEdit = new QLineEdit(entry.topic, &dlg);
    auto *conclusionEdit = new QLineEdit(entry.conclusion, &dlg);
    auto *sourcesEdit = new QLineEdit(entry.sources, &dlg);
    form->addRow("主题:", topicEdit);
    form->addRow("结论:", conclusionEdit);
    form->addRow("来源:", sourcesEdit);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);

    if (dlg.exec() == QDialog::Accepted) {
        entry.topic = topicEdit->text().trimmed();
        entry.conclusion = conclusionEdit->text().trimmed();
        entry.sources = sourcesEdit->text().trimmed();
        if (!entry.topic.isEmpty()) {
            m_db->updateKnowledge(entry.id, entry);
            // 同步到 ChromaDB
            auto updated = m_db->getKnowledgeById(entry.id);
            syncKnowledgeToChroma(entry.id, entry.topic, entry.conclusion,
                                 entry.sources, updated.createdAt);
            refresh();
        }
    }
}

void MemoryPage::onDelete()
{
    int row = m_list->currentRow();
    if (row < 0 || row >= m_idCache.size() || !m_db) return;
    qint64 id = m_idCache[row];
    auto entry = m_db->getKnowledgeById(id);

    auto result = QMessageBox::question(
        this, "确认删除",
        QString("确定删除「%1」？").arg(entry.topic),
        QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes) {
        deleteKnowledgeFromChroma(id);
        m_db->deleteKnowledge(id);
        refresh();
    }
}

void MemoryPage::refresh()
{
    if (m_semanticMode) {
        onSemanticSearch();
    } else {
        onSearch();
    }
}

// ============================================================
// 语义搜索
// ============================================================

void MemoryPage::onSemanticSearch()
{
    QString kw = m_searchInput->text().trimmed();
    if (kw.isEmpty()) {
        // 空查询回退到关键词搜索
        m_semanticMode = false;
        onSearch();
        return;
    }

    if (m_cliProcess && m_cliProcess->state() != QProcess::NotRunning) {
        return;  // 已有 CLI 进程在运行
    }

    QString agentDir = m_agentDir;
    if (agentDir.isEmpty()) {
        agentDir = QDir::currentPath() + "/agent";
    }
    QString cliScript = agentDir + "/cache_cli.py";

    m_cliProcess = new QProcess(this);
    connect(m_cliProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MemoryPage::onCliProcessFinished);

    QStringList args;
    args << cliScript << "knowledge_search" << kw << "--top_k" << "20";
    m_cliProcess->start(m_pythonPath, args);

    m_semanticBtn->setProperty("cssClass", "primary");
    // 强制刷新样式
    m_semanticBtn->style()->unpolish(m_semanticBtn);
    m_semanticBtn->style()->polish(m_semanticBtn);
}

void MemoryPage::onCliProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_cliProcess) return;

    QByteArray output = m_cliProcess->readAllStandardOutput();
    m_cliProcess->deleteLater();
    m_cliProcess = nullptr;

    if (exitCode != 0 || status == QProcess::CrashExit) {
        m_semanticBtn->setProperty("cssClass", "secondary");
        m_semanticBtn->style()->unpolish(m_semanticBtn);
        m_semanticBtn->style()->polish(m_semanticBtn);
        return;
    }

    // 解析 JSON 结果
    QJsonDocument doc = QJsonDocument::fromJson(output);
    if (doc.isNull()) return;

    QJsonArray results = doc.object()["results"].toArray();

    m_list->clear();
    m_idCache.clear();
    m_detailView->clear();
    m_semanticMode = true;

    for (const auto &r : results) {
        QJsonObject obj = r.toObject();
        qint64 sqliteId = obj["sqlite_id"].toVariant().toLongLong();
        QString topic = obj["topic"].toString();
        double sim = obj["similarity"].toDouble();

        m_list->addItem(QString("[%1%] %2")
                            .arg(static_cast<int>(sim * 100))
                            .arg(topic));
        m_idCache.append(sqliteId);
    }
    m_countLabel->setText(QString("共 %1 条 (语义)").arg(results.size()));

    // 恢复按钮样式
    m_semanticBtn->setProperty("cssClass", "secondary");
    m_semanticBtn->style()->unpolish(m_semanticBtn);
    m_semanticBtn->style()->polish(m_semanticBtn);
}

// ============================================================
// ChromaDB 同步
// ============================================================

void MemoryPage::syncKnowledgeToChroma(qint64 sqliteId, const QString &topic,
                                       const QString &conclusion,
                                       const QString &sources,
                                       const QString &createdAt)
{
    if (m_cliProcess && m_cliProcess->state() != QProcess::NotRunning) return;

    QString agentDir = m_agentDir;
    if (agentDir.isEmpty()) agentDir = QDir::currentPath() + "/agent";
    QString cliScript = agentDir + "/cache_cli.py";

    auto *proc = new QProcess(this);
    QStringList args;
    args << cliScript << "knowledge_sync"
         << "--action" << "upsert"
         << "--sqlite_id" << QString::number(sqliteId)
         << "--topic" << topic
         << "--conclusion" << conclusion
         << "--sources" << sources
         << "--created_at" << createdAt;
    proc->start(m_pythonPath, args);
    // fire-and-forget
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            proc, &QProcess::deleteLater);
}

void MemoryPage::deleteKnowledgeFromChroma(qint64 sqliteId)
{
    QString agentDir = m_agentDir;
    if (agentDir.isEmpty()) agentDir = QDir::currentPath() + "/agent";
    QString cliScript = agentDir + "/cache_cli.py";

    auto *proc = new QProcess(this);
    QStringList args;
    args << cliScript << "knowledge_sync"
         << "--action" << "delete"
         << "--sqlite_id" << QString::number(sqliteId);
    proc->start(m_pythonPath, args);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            proc, &QProcess::deleteLater);
}
