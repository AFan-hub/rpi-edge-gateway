#ifndef DATA_PROCESSOR_HPP
#define DATA_PROCESSOR_HPP

#include <cstdint>

#include "Protocol.hpp"

// Consumes decoded frames off the FrameQueue. Day 11 deliverable:
// this is the ProcessThread body, decoupled from both serial RX and TCP TX.
class DataProcessor
{
public:
    void handle(const protocol::Frame &frame);

    uint64_t processed() const { return processed_; }
    const protocol::SensorPayload &last() const { return last_; }

private:
    uint64_t processed_ = 0;
    protocol::SensorPayload last_{};
};

#endif
