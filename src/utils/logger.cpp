#include "logger.h"

#include <QDir>
#include <QDateTime>
#include <QTextStream>
#include <cstdarg>
#include <cstdio>

Logger *Logger::instance()
{
    static Logger inst;
    return &inst;
}

void Logger::setLogDir(const QString &dir)
{
    QMutexLocker locker(&m_mutex);
    m_logDir = dir;
    if (!m_logDir.endsWith('/') && !m_logDir.endsWith('\\'))
        m_logDir += '/';
    QDir().mkpath(m_logDir);
    cleanOldLogs();
}

void Logger::setLevel(Level level)
{
    QMutexLocker locker(&m_mutex);
    m_level = level;
}

void Logger::setRetentionDays(int days)
{
    QMutexLocker locker(&m_mutex);
    m_retentionDays = qMax(1, days);
}

void Logger::log(Level level, const char *module, const char *fmt, ...)
{
    if (level > m_level) return;

    QMutexLocker locker(&m_mutex);

    // 检查是否需要轮转
    rotateIfNeeded();

    // 格式化消息
    va_list args1, args2;
    va_start(args1, fmt);
    va_copy(args2, args1);
    int len = vsnprintf(nullptr, 0, fmt, args1) + 1;
    va_end(args1);
    QByteArray buf(len, Qt::Uninitialized);
    vsnprintf(buf.data(), len, fmt, args2);
    va_end(args2);

    // 构建日志行
    static const char *levelNames[] = {"ERROR", "WARN", "INFO", "DEBUG"};
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString line = QString("[%1] [%2] [%3] %4\n")
                       .arg(timestamp, levelNames[level], module,
                            QString::fromUtf8(buf).trimmed());

    // 写入文件
    if (m_file.isOpen()) {
        m_file.write(line.toUtf8());
        m_file.flush();
    }
}

void Logger::rotateIfNeeded()
{
    QString today = QDate::currentDate().toString("yyyy-MM-dd");

    if (m_currentDate == today && m_file.isOpen())
        return;

    // 关闭旧文件
    if (m_file.isOpen())
        m_file.close();

    // 打开新文件
    m_currentDate = today;
    QString path = currentLogPath();
    QDir().mkpath(m_logDir);
    m_file.setFileName(path);
    m_file.open(QIODevice::Append | QIODevice::Text);

    // 写入启动标记
    QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    m_file.write(QString("\n=== Certus 日志启动 %1 ===\n").arg(stamp).toUtf8());
    m_file.flush();
}

void Logger::cleanOldLogs()
{
    QDir dir(m_logDir);
    QStringList filters;
    filters << "certus_*.log";

    QDateTime cutoff = QDateTime::currentDateTime().addDays(-m_retentionDays);
    for (const QString &name : dir.entryList(filters, QDir::Files)) {
        // 从文件名提取日期：certus_2026-06-14.log
        QString dateStr = name.mid(7, 10);  // "2026-06-14"
        QDate fileDate = QDate::fromString(dateStr, "yyyy-MM-dd");
        if (fileDate.isValid()
            && QDateTime(fileDate, QTime(0, 0)) < cutoff) {
            dir.remove(name);
        }
    }
}

QString Logger::currentLogPath() const
{
    return m_logDir + "certus_" + m_currentDate + ".log";
}
