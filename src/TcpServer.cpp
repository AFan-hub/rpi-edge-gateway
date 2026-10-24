#include "TcpServer.hpp"

#include <string>

#include "Logger.hpp"

bool TcpServer::start(uint16_t port)
{
    // TODO(Day 12): socket -> bind -> listen -> fcntl(O_NONBLOCK) ->
    // epoll_create1 -> epoll_ctl(listen fd) -> epoll_wait loop handling
    // accept / recv / disconnect in a single NetworkThread.
    logging::warn("TcpServer::start(" + std::to_string(port) +
              ") is a stub until Day 12");
    return false;
}

void TcpServer::stop()
{
}

void TcpServer::broadcast(const uint8_t * /*data*/, size_t /*len*/)
{
}
