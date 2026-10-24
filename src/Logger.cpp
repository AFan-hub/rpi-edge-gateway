#include "Logger.hpp"

#include <cstdio>
#include <ctime>
#include <mutex>

namespace logging
{

namespace
{

std::mutex g_logMutex;

void write(const char *level, const std::string &msg)
{
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);

    std::lock_guard<std::mutex> lock(g_logMutex);
    std::printf("[%5ld.%03ld] [%-5s] %s\n",
                static_cast<long>(ts.tv_sec),
                static_cast<long>(ts.tv_nsec / 1000000),
                level,
                msg.c_str());
    std::fflush(stdout);
}

} // namespace

void info(const std::string &msg)  { write("INFO",  msg); }
void warn(const std::string &msg)  { write("WARN",  msg); }
void error(const std::string &msg) { write("ERROR", msg); }

} // namespace logging
