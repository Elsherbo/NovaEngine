// ============================================================
// FILE:    engine/core/log.h
// MODULE:  Core > Log
// PHASE:   1
// STATUS:  TODO
// PURPOSE: Engine-wide logging with levels. Output to
//          console (stdout/stderr) and optional file.
// DEPENDS: (none)
// ============================================================
#pragma once

#include <cstdio>
#include <cstddef>
#include <functional>

namespace nova
{

enum class LogLevel : int
{
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

class Logger
{
public:
    static Logger &instance();

    void setLevel(LogLevel level);
    void setFile(FILE *file);
    void setFilePath(const char *path);

    // Optional hook called for every message that passes the level filter.
    // Set once after the console is created; pass nullptr to clear.
    using Hook = std::function<void(LogLevel, const char*)>;
    void setHook(Hook hook) { m_hook = std::move(hook); }

    void debug(const char *fmt, ...);
    void info(const char *fmt, ...);
    void warn(const char *fmt, ...);
    void error(const char *fmt, ...);

    void log(LogLevel level, const char *msg);

private:
    Logger();
    ~Logger();

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    void writePrefix(LogLevel level, const char *levelStr);

    LogLevel m_level = LogLevel::Debug;
    FILE *m_file = nullptr;
    bool m_ownsFile = false;
    Hook m_hook;
};

#define LOG_DEBUG(msg) ::nova::Logger::instance().debug(msg)
#define LOG_INFO(msg) ::nova::Logger::instance().info(msg)
#define LOG_WARN(msg) ::nova::Logger::instance().warn(msg)
#define LOG_ERROR(msg) ::nova::Logger::instance().error(msg)

} // namespace nova