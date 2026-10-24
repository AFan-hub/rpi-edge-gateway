#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>

// Not named `log`: that identifier is a GCC built-in (math logarithm) and
// declaring it as a namespace triggers -Wbuiltin-declaration-mismatch.
namespace logging
{

// Thread-safe, monotonic-timestamped stdout logging.
void info(const std::string &msg);
void warn(const std::string &msg);
void error(const std::string &msg);

} // namespace logging

#endif
