#pragma once
/**
 * 滚动日志文件系统。
 *
 *   - 每天一个文件：logs/certus_2026-06-14.log
 *   - 格式：[14:31:02] [ERROR] [module] message
 *   - 启动时自动清理超过保留天数的旧文件
 *   - 线程安全：QMutex 保护写操作
 *   - 使用宏调用：LOG_INFO("agent", "消息 %d", value)
 */

#include <QString>
#include <QFile>
#include <QMutex>

class Logger {
public:
    enum Level { Error = 0, Warning = 1, Info = 2, Debug = 3 };

    static Logger *instance();

    void setLogDir(const QString &dir);       // 默认 "logs/"
    void setLevel(Level level);               // 默认 Warning
    void setRetentionDays(int days);          // 默认 7

    void log(Level level, const char *module, const char *fmt, ...);

    bool isEnabled(Level level) const { return level <= m_level; }

private:
    Logger() = default;

    void rotateIfNeeded();                    // 每天一个文件
    void cleanOldLogs();                      // 清理过期文件
    QString currentLogPath() const;

    QString m_logDir = "logs";
    Level m_level = Info;
    int m_retentionDays = 7;
    QString m_currentDate;
    QFile m_file;
    QMutex m_mutex;
};

// 便捷宏
#define LOG_ERROR(mod, fmt, ...) \
    Logger::instance()->log(Logger::Error, mod, fmt, ##__VA_ARGS__)
#define LOG_WARN(mod, fmt, ...) \
    Logger::instance()->log(Logger::Warning, mod, fmt, ##__VA_ARGS__)
#define LOG_INFO(mod, fmt, ...) \
    Logger::instance()->log(Logger::Info, mod, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(mod, fmt, ...) \
    Logger::instance()->log(Logger::Debug, mod, fmt, ##__VA_ARGS__)
