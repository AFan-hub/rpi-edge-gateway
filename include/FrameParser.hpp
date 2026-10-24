#ifndef FRAME_PARSER_HPP
#define FRAME_PARSER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "Protocol.hpp"

// Streaming frame parser.
//
// feed() may be called with arbitrary chunks: half a frame, one and a half
// frames, several frames at once, garbage bytes in between. Never assume one
// read() returns exactly one frame. On CRC error or an impossible
// version/LEN the parser drops back to SearchHead1 and resynchronizes.
class FrameParser
{
public:
    struct Stats
    {
        uint64_t framesOk  = 0;
        uint64_t crcErrors = 0;
        uint64_t resyncs   = 0; // bad version or impossible LEN
    };

    using Callback = std::function<void(const protocol::Frame &)>;

    explicit FrameParser(Callback onFrame);

    void feed(const uint8_t *data, size_t len);

    const Stats &stats() const { return stats_; }

private:
    enum class State
    {
        SearchHead1,
        SearchHead2,
        ReadHeader,
        ReadPayload,
        ReadCrc,
    };

    void processByte(uint8_t byte);

    Callback onFrame_;
    State state_ = State::SearchHead1;

    uint8_t  header_[4] = {0}; // VER CMD LEN_L LEN_H
    size_t   headerIdx_ = 0;
    uint8_t  version_ = 0;
    uint8_t  command_ = 0;
    uint16_t payloadLen_ = 0;
    std::vector<uint8_t> payload_;
    size_t   payloadIdx_ = 0;
    uint8_t  crcBuf_[2] = {0};
    size_t   crcIdx_ = 0;

    Stats stats_;
};

#endif
