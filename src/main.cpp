#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <thread>

#include "DataProcessor.hpp"
#include "FrameParser.hpp"
#include "Logger.hpp"
#include "SerialPort.hpp"
#include "TcpServer.hpp"
#include "ThreadSafeQueue.hpp"

namespace
{

std::atomic<bool> g_running{true};
ThreadSafeQueue<protocol::Frame> g_frameQueue;

void onSignal(int)
{
    /* Ctrl+C: flip the flag AND wake every thread blocked on the queue,
     * otherwise join() below would hang forever. */
    g_running.store(false);
    g_frameQueue.stop();
}

} // namespace

int main(int argc, char **argv)
{
    const char *device   = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    const int   baudrate = (argc > 2) ? std::atoi(argv[2]) : 115200;
    const uint16_t port  = (argc > 3) ? (uint16_t)std::atoi(argv[3]) : 9000;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    SerialPort port_dev(device, baudrate);

    if (!port_dev.openPort())
    {
        logging::error("cannot open " + std::string(device) + ": " +
                       std::strerror(errno));
        return 1;
    }

    TcpServer server;

    if (!server.start(port))
    {
        logging::warn("tcp server failed to start; continuing with logs only");
    }

    /* ProcessThread -> TCP broadcast: one text line per sensor payload. */
    DataProcessor processor([&server](const protocol::SensorPayload &p) {
        char line[96];
        int n = std::snprintf(line, sizeof(line),
                              "[data] ts=%u s1=%.2f s2=%.2f status=0x%04X\n",
                              static_cast<unsigned>(p.timestamp_ms),
                              static_cast<double>(p.sensor_1),
                              static_cast<double>(p.sensor_2),
                              static_cast<unsigned>(p.status));
        if (n > 0)
        {
            server.push(reinterpret_cast<const uint8_t *>(line),
                        static_cast<size_t>(n));
        }
    });

    FrameParser parser([&](const protocol::Frame &frame) {
        g_frameQueue.push(frame);
    });

    std::thread rxThread([&] {
        uint8_t buf[512];

        while (g_running.load())
        {
            const ssize_t n = port_dev.readData(buf, sizeof(buf));

            if (n > 0)
            {
                /* One read() may hold half a frame or three frames:
                 * the parser, not this loop, owns frame boundaries. */
                parser.feed(buf, static_cast<size_t>(n));
            }
            else if (n == 0 || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            else
            {
                logging::error(std::string("serial read error: ") +
                               std::strerror(errno));
                break;
            }
        }
    });

    std::thread processThread([&] {
        protocol::Frame frame;

        while (g_running.load())
        {
            if (g_frameQueue.waitAndPopFor(frame, std::chrono::milliseconds(200)))
            {
                processor.handle(frame);
            }
        }
    });

    /* Day 13: periodic health line - RX FPS, CRC errors, resyncs, queue
     * depth, live client count, processed frames. Goes to stdout, which
     * systemd captures into the journal. */
    std::thread monitorThread([&] {
        uint64_t lastOk = 0;
        auto lastT = std::chrono::steady_clock::now();

        while (g_running.load())
        {
            for (int i = 0; i < 50 && g_running.load(); ++i)   // 5 s in 100 ms steps
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (!g_running.load())
            {
                break;
            }

            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - lastT).count();
            lastT = now;

            uint64_t ok = parser.stats().framesOk.load();
            double fps = (dt > 0.0) ? (static_cast<double>(ok - lastOk) / dt) : 0.0;
            lastOk = ok;

            char line[160];
            std::snprintf(line, sizeof(line),
                          "[monitor] fps=%.1f frames=%llu crc_err=%llu resync=%llu "
                          "queue=%zu clients=%d processed=%llu",
                          fps,
                          static_cast<unsigned long long>(ok),
                          static_cast<unsigned long long>(parser.stats().crcErrors.load()),
                          static_cast<unsigned long long>(parser.stats().resyncs.load()),
                          g_frameQueue.size(),
                          server.clientCount(),
                          static_cast<unsigned long long>(processor.processed()));
            logging::info(line);
        }
    });

    logging::info("edge gateway running on " + std::string(device));

    rxThread.join();
    processThread.join();
    monitorThread.join();
    server.stop();

    const auto &s = parser.stats();
    std::printf("[shutdown] frames_ok=%llu crc_errors=%llu resyncs=%llu "
                "processed=%llu tcp_pushed=%llu tcp_dropped_clients=%llu\n",
                static_cast<unsigned long long>(s.framesOk),
                static_cast<unsigned long long>(s.crcErrors),
                static_cast<unsigned long long>(s.resyncs),
                static_cast<unsigned long long>(processor.processed()),
                static_cast<unsigned long long>(server.pushedBytes()),
                static_cast<unsigned long long>(server.droppedClients()));

    return 0;
}
