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
#include "ThreadSafeQueue.hpp"

namespace
{

std::atomic<bool> g_running{true};
ThreadSafeQueue<protocol::Frame> g_frameQueue;

void onSignal(int)
{
    // Ctrl+C: flip the flag AND wake every thread blocked on the queue,
    // otherwise join() below would hang forever.
    g_running.store(false);
    g_frameQueue.stop();
}

} // namespace

int main(int argc, char **argv)
{
    const char *device   = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    const int   baudrate = (argc > 2) ? std::atoi(argv[2]) : 115200;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    SerialPort port(device, baudrate);

    if (!port.openPort())
    {
        logging::error("cannot open " + std::string(device) + ": " +
                   std::strerror(errno));
        return 1;
    }

    DataProcessor processor;

    FrameParser parser([&](const protocol::Frame &frame) {
        g_frameQueue.push(frame);
    });

    std::thread rxThread([&] {
        uint8_t buf[512];

        while (g_running.load())
        {
            const ssize_t n = port.readData(buf, sizeof(buf));

            if (n > 0)
            {
                // One read() may hold half a frame or three frames:
                // the parser, not this loop, owns frame boundaries.
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

    logging::info("edge gateway running on " + std::string(device));

    rxThread.join();
    processThread.join();

    const auto &s = parser.stats();
    std::printf("[shutdown] frames_ok=%llu crc_errors=%llu resyncs=%llu processed=%llu\n",
                static_cast<unsigned long long>(s.framesOk),
                static_cast<unsigned long long>(s.crcErrors),
                static_cast<unsigned long long>(s.resyncs),
                static_cast<unsigned long long>(processor.processed()));

    return 0;
}
