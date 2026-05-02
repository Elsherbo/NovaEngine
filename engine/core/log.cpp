#include "engine/core/log.h"

#include <cstdarg>
#include <cstring>
#include <ctime>

namespace nova
{

Logger::Logger()
    : m_file(stderr)
{
}

Logger::~Logger()
{
    if (m_ownsFile && m_file)
    {
        fclose(m_file);
    }
}

Logger &Logger::instance()
{
    static Logger instance;
    return instance;
}

void Logger::setLevel(LogLevel level)
{
    m_level = level;
}

void Logger::setFile(FILE *file)
{
    if (m_ownsFile && m_file)
    {
        fclose(m_file);
        m_ownsFile = false;
    }
    m_file = file;
}

void Logger::setFilePath(const char *path)
{
    if (m_ownsFile && m_file)
    {
        fclose(m_file);
    }
    m_file = fopen(path, "a");
    m_ownsFile = (m_file != nullptr);
}

void Logger::writePrefix(LogLevel /*level*/, const char *levelStr)
{
    if (!m_file)
        return;

    time_t now = time(nullptr);
    struct tm tmbuf;
#ifdef _WIN32
    localtime_s(&tmbuf, &now);
#else
    localtime_r(&now, &tmbuf);
#endif

    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmbuf);

    fprintf(m_file, "[%s] [%s] ", timeBuf, levelStr);
}

void Logger::debug(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(LogLevel::Debug, buf);
}

void Logger::info(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(LogLevel::Info, buf);
}

void Logger::warn(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(LogLevel::Warn, buf);
}

void Logger::error(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(LogLevel::Error, buf);
}

void Logger::log(LogLevel level, const char *msg)
{
    if (level < m_level)
        return;

    if (!m_file)
        return;

    const char *levelStr = "???";
    switch (level)
    {
    case LogLevel::Debug:
        levelStr = "DEBUG";
        break;
    case LogLevel::Info:
        levelStr = "INFO";
        break;
    case LogLevel::Warn:
        levelStr = "WARN";
        break;
    case LogLevel::Error:
        levelStr = "ERROR";
        break;
    }

    writePrefix(level, levelStr);
    fprintf(m_file, "%s\n", msg);
    fflush(m_file);

    if (m_hook)
        m_hook(level, msg);
}

} // namespace nova