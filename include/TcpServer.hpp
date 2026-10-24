#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

#include <cstddef>
#include <cstdint>

// Day 12 deliverable: non-blocking listen socket + epoll, one NetworkThread
// serving every client (never one thread per client).
class TcpServer
{
public:
    bool start(uint16_t port);
    void stop();

    // Push raw bytes to every connected client.
    void broadcast(const uint8_t *data, size_t len);
};

#endif
