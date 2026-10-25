#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

// Non-blocking TCP server driven by a single epoll loop (manual §28).
// One thread serves every client - never one thread per client.
//
// Producers (ProcessThread) call push() from any thread; an eventfd wakes
// the epoll loop, which fans the payload out to every client through a
// per-client output buffer, so partial writes and slow clients cannot
// block the loop or each other. A client whose backlog exceeds the cap is
// disconnected (back-pressure policy) and counted.
class TcpServer
{
public:
    TcpServer() = default;
    ~TcpServer();

    TcpServer(const TcpServer &) = delete;
    TcpServer &operator=(const TcpServer &) = delete;

    bool start(uint16_t port);
    void stop();

    // Thread-safe broadcast: copies data into the pending buffer, wakes loop.
    bool push(const uint8_t *data, size_t len);

    int clientCount() const;
    uint64_t droppedClients() const { return droppedClients_; }
    uint64_t pushedBytes() const { return pushedBytes_; }

private:
    struct Client
    {
        int fd = -1;
        std::vector<uint8_t> out;   // pending bytes for this client
    };

    void run();
    void handleAccept();
    void handleReadable(Client &c);
    void handleWritable(Client &c);
    void removeClient(size_t index);
    void flushBroadcast();
    int  indexOfFd(int fd) const;

    int listenFd_ = -1;
    int epollFd_ = -1;
    int wakeFd_ = -1;
    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex clientsMutex_;   // guards clients_ (push/status vs loop)
    std::vector<Client> clients_;

    std::mutex queueMutex_;
    std::vector<uint8_t> broadcast_;

    std::atomic<uint64_t> droppedClients_{0};
    std::atomic<uint64_t> pushedBytes_{0};
};

#endif
