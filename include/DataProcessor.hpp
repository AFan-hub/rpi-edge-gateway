#ifndef DATA_PROCESSOR_HPP
#define DATA_PROCESSOR_HPP

#include <cstdint>
#include <functional>
#include <utility>

#include "Protocol.hpp"

// Consumes decoded frames off the FrameQueue and forwards sensor payloads
// to a sink (the TCP broadcast in main). This is the ProcessThread body,
// decoupled from both serial RX and TCP TX.
class DataProcessor
{
public:
    using Sink = std::function<void(const protocol::SensorPayload &)>;

    explicit DataProcessor(Sink sink = {})
        : sink_(std::move(sink))
    {
    }

    void handle(const protocol::Frame &frame);

    uint64_t processed() const { return processed_; }
    const protocol::SensorPayload &last() const { return last_; }

private:
    Sink sink_;
    uint64_t processed_ = 0;
    protocol::SensorPayload last_{};
};

#endif
