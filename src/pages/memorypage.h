#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QProcess>

class Database;

class MemoryPage : public QWidget {
    Q_OBJECT
public:
    explicit MemoryPage(QWidget *parent = nullptr);
    void setDatabase(Database *db);
    void setPythonPath(const QString &path);
    void setAgentDir(const QString &dir);
    void refresh();

private slots:
    void onSearch();
    void onSemanticSearch();
    void onItemSelected(QListWidgetItem *item);
    void onAdd();
    void onEdit();
    void onDelete();
    void onCliProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void setupUi();
    void loadDetail(qint64 id);
    void syncKnowledgeToChroma(qint64 sqliteId, const QString &topic,
                               const QString &conclusion, const QString &sources,
                               const QString &createdAt);
    void deleteKnowledgeFromChroma(qint64 sqliteId);

    Database *m_db = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QListWidget *m_list = nullptr;
    QTextEdit *m_detailView = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_editBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QPushButton *m_semanticBtn = nullptr;  // 语义搜索按钮

    // id 缓存：与 m_list 行号一一对应
    QList<qint64> m_idCache;

    // Python CLI 调用
    QString m_pythonPath = "python";
    QString m_agentDir;
    QProcess *m_cliProcess = nullptr;
    bool m_semanticMode = false;  // 当前是否为语义搜索模式
};
