#include <cstdio>
#include <cassert>

#include "engine/core/log.h"

int main()
{
    using namespace nova;

    Logger &log = Logger::instance();

    log.setLevel(LogLevel::Debug);
    log.setFile(stdout);

    log.debug("debug message");
    log.info("info message");
    log.warn("warning message");
    log.error("error message");

    log.setLevel(LogLevel::Error);
    log.debug("should not appear");
    log.info("should not appear either");
    log.error("error after filter up");

    printf("test_log: all passed\n");
    return 0;
}