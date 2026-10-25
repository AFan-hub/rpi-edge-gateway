#include "TcpServer.hpp"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Logger.hpp"

namespace
{
constexpr int    kMaxClients      = 16;
constexpr size_t kClientBufCap    = 64 * 1024;   // slow-client cutoff
constexpr size_t kBroadcastCap    = 64 * 1024;   // pending fan-out cap
constexpr int    kMaxEvents       = 32;

int setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        return -1;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

} // namespace

TcpServer::~TcpServer()
{
    stop();
}

bool TcpServer::start(uint16_t port)
{
    if (running_.load())
    {
        return false;
    }

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0)
    {
        logging::error(std::string("socket: ") + std::strerror(errno));
        return false;
    }

    int one = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    if (setNonBlocking(listenFd_) != 0)
    {
        logging::error("fcntl O_NONBLOCK failed");
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(listenFd_, reinterpret_cast<sockaddr *>(&addr),
               sizeof(addr)) != 0)
    {
        logging::error(std::string("bind: ") + std::strerror(errno));
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (::listen(listenFd_, kMaxClients) != 0)
    {
        logging::error(std::string("listen: ") + std::strerror(errno));
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    epollFd_ = ::epoll_create1(EPOLL_CLOEXEC);
    wakeFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    if (epollFd_ < 0 || wakeFd_ < 0)
    {
        logging::error("epoll/eventfd create failed");
        stop();
        return false;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listenFd_;
    ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenFd_, &ev);

    ev.data.fd = wakeFd_;
    ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, wakeFd_, &ev);

    running_.store(true);
    thread_ = std::thread(&TcpServer::run, this);

    logging::info("tcp server listening on 0.0.0.0:" + std::to_string(port));
    return true;
}

void TcpServer::stop()
{
    if (!running_.exchange(false))
    {
        return;
    }

    uint64_t one = 1;
    ssize_t rc = ::write(wakeFd_, &one, sizeof(one));
    (void)rc;

    if (thread_.joinable())
    {
        thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto &c : clients_)
        {
            ::close(c.fd);
        }
        clients_.clear();
    }

    if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
    if (epollFd_ >= 0)  { ::close(epollFd_);  epollFd_ = -1; }
    if (wakeFd_ >= 0)   { ::close(wakeFd_);   wakeFd_ = -1; }
}

bool TcpServer::push(const uint8_t *data, size_t len)
{
    if (!running_.load() || len == 0)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);

        if (broadcast_.size() + len > kBroadcastCap)
        {
            return false;   // consumer slower than producer: drop this push
        }

        broadcast_.insert(broadcast_.end(), data, data + len);
        pushedBytes_ += len;
    }

    uint64_t one = 1;
    ssize_t rc = ::write(wakeFd_, &one, sizeof(one));
    (void)rc;
    return true;
}

int TcpServer::clientCount() const
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return static_cast<int>(clients_.size());
}

int TcpServer::indexOfFd(int fd) const
{
    for (size_t i = 0; i < clients_.size(); ++i)
    {
        if (clients_[i].fd == fd)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void TcpServer::run()
{
    epoll_event events[kMaxEvents];

    while (running_.load())
    {
        int n = ::epoll_wait(epollFd_, events, kMaxEvents, -1);

        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }

        for (int i = 0; i < n && running_.load(); ++i)
        {
            int fd = events[i].data.fd;

            if (fd == listenFd_)
            {
                handleAccept();
            }
            else if (fd == wakeFd_)
            {
                uint64_t drain = 0;
                ssize_t rc = ::read(wakeFd_, &drain, sizeof(drain));
                (void)rc;
                flushBroadcast();
            }
            else
            {
                /* clients_ is also read by clientCount()/stop() from other
                 * threads: hold the lock for the whole client event. */
                std::lock_guard<std::mutex> lock(clientsMutex_);

                int idx = indexOfFd(fd);
                if (idx < 0)
                {
                    continue;
                }

                if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
                {
                    removeClient(static_cast<size_t>(idx));
                    continue;
                }

                if (events[i].events & EPOLLIN)
                {
                    handleReadable(clients_[static_cast<size_t>(idx)]);
                }

                idx = indexOfFd(fd);   // may have been removed above
                if (idx >= 0 && (events[i].events & EPOLLOUT))
                {
                    handleWritable(clients_[static_cast<size_t>(idx)]);
                }
            }
        }
    }
}

void TcpServer::handleAccept()
{
    for (;;)
    {
        sockaddr_in peer{};
        socklen_t peerLen = sizeof(peer);
        int fd = ::accept(listenFd_, reinterpret_cast<sockaddr *>(&peer),
                          &peerLen);

        if (fd < 0)
        {
            return;   // EAGAIN: all pending connections accepted
        }

        if (setNonBlocking(fd) != 0)
        {
            ::close(fd);
            continue;
        }

        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        std::lock_guard<std::mutex> lock(clientsMutex_);

        if (static_cast<int>(clients_.size()) >= kMaxClients)
        {
            ::close(fd);   // over capacity: refuse politely
            ++droppedClients_;
            continue;
        }

        Client c;
        c.fd = fd;
        clients_.push_back(std::move(c));

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = fd;

        if (::epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) != 0)
        {
            ::close(fd);
            clients_.pop_back();
            continue;
        }

        char ip[32] = "?";
        (void)ip;   // inet_ntop optional; logging the fd is enough for v1
        logging::info("client connected, fd=" + std::to_string(fd) +
                      " total=" + std::to_string(clients_.size()));
    }
}

void TcpServer::handleReadable(Client &c)
{
    uint8_t buf[4096];

    for (;;)
    {
        ssize_t n = ::recv(c.fd, buf, sizeof(buf), 0);

        if (n > 0)
        {
            continue;   // v1: client->server payload is drained and ignored
        }

        if (n == 0)
        {
            int idx = indexOfFd(c.fd);
            if (idx >= 0)
            {
                removeClient(static_cast<size_t>(idx));
            }
            return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return;
        }

        int idx = indexOfFd(c.fd);
        if (idx >= 0)
        {
            removeClient(static_cast<size_t>(idx));
        }
        return;
    }
}

void TcpServer::handleWritable(Client &c)
{
    while (!c.out.empty())
    {
        ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);

        if (n > 0)
        {
            c.out.erase(c.out.begin(), c.out.begin() + n);
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            return;   // kernel buffer full: keep EPOLLOUT registered
        }

        int idx = indexOfFd(c.fd);
        if (idx >= 0)
        {
            removeClient(static_cast<size_t>(idx));
        }
        return;
    }

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLRDHUP;
    ev.data.fd = c.fd;
    ::epoll_ctl(epollFd_, EPOLL_CTL_MOD, c.fd, &ev);
}

void TcpServer::flushBroadcast()
{
    std::vector<uint8_t> pending;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (broadcast_.empty())
        {
            return;
        }
        pending.swap(broadcast_);
    }

    std::lock_guard<std::mutex> lock(clientsMutex_);

    for (size_t i = 0; i < clients_.size();)
    {
        Client &c = clients_[i];

        if (c.out.size() + pending.size() > kClientBufCap)
        {
            logging::warn("client fd=" + std::to_string(c.fd) +
                          " backlog over cap, dropping (slow consumer)");
            ++droppedClients_;
            removeClient(i);
            continue;
        }

        bool wasEmpty = c.out.empty();
        c.out.insert(c.out.end(), pending.begin(), pending.end());

        if (wasEmpty)
        {
            epoll_event ev{};
            ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
            ev.data.fd = c.fd;
            ::epoll_ctl(epollFd_, EPOLL_CTL_MOD, c.fd, &ev);
        }

        ++i;
    }
}

void TcpServer::removeClient(size_t index)
{
    if (index >= clients_.size())
    {
        return;
    }

    int fd = clients_[index].fd;
    ::close(fd);
    clients_.erase(clients_.begin() + static_cast<long>(index));

    logging::info("client disconnected, fd=" + std::to_string(fd) +
                  " remaining=" + std::to_string(clients_.size()));
}
