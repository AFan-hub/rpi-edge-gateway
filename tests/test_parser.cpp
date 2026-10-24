#include <cassert>
#include <cstdio>
#include <vector>

#include "FrameParser.hpp"
#include "Protocol.hpp"

namespace
{

std::vector<uint8_t> sensorPayload(uint32_t ts, float s1, float s2)
{
    protocol::SensorPayload s{ts, s1, s2, 0};
    uint8_t buf[64];
    const size_t n = protocol::encodeSensor(s, buf, sizeof(buf));
    assert(n > protocol::HEADER_SIZE + protocol::CRC_SIZE);
    return std::vector<uint8_t>(buf + protocol::HEADER_SIZE,
                                buf + n - protocol::CRC_SIZE);
}

std::vector<uint8_t> makeFrame(uint8_t cmd, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> out(protocol::HEADER_SIZE + payload.size() +
                             protocol::CRC_SIZE);
    const size_t n = protocol::encode(static_cast<protocol::Command>(cmd),
                                      payload.data(),
                                      static_cast<uint16_t>(payload.size()),
                                      out.data(), out.size());
    assert(n == out.size());
    return out;
}

} // namespace

int main()
{
    // Case 1: one complete frame in a single feed.
    {
        std::vector<protocol::Frame> got;
        FrameParser p([&](const protocol::Frame &f) { got.push_back(f); });

        const auto f1 = makeFrame(0x01, sensorPayload(1, 1.0f, 2.0f));
        p.feed(f1.data(), f1.size());

        assert(got.size() == 1);
        assert(p.stats().framesOk == 1);
    }

    // Case 2: half a frame now, the rest later (half packet).
    {
        std::vector<protocol::Frame> got;
        FrameParser p([&](const protocol::Frame &f) { got.push_back(f); });

        const auto f1 = makeFrame(0x01, sensorPayload(2, 3.0f, 4.0f));
        p.feed(f1.data(), 5);
        assert(got.empty());

        p.feed(f1.data() + 5, f1.size() - 5);
        assert(got.size() == 1);
    }

    // Case 3: two frames inside one read (sticky packets).
    {
        std::vector<protocol::Frame> got;
        FrameParser p([&](const protocol::Frame &f) { got.push_back(f); });

        auto both = makeFrame(0x01, sensorPayload(3, 1.0f, 1.0f));
        const auto f2 = makeFrame(0x06, {});
        both.insert(both.end(), f2.begin(), f2.end());

        p.feed(both.data(), both.size());

        assert(got.size() == 2);
        assert(got[1].cmd == protocol::Command::Heartbeat);
    }

    // Case 4: garbage bytes in front of a valid frame.
    {
        std::vector<protocol::Frame> got;
        FrameParser p([&](const protocol::Frame &f) { got.push_back(f); });

        const uint8_t junk[] = {0x00, 0xFF, 0xAA, 0x11, 0x55, 0x77};
        p.feed(junk, sizeof(junk));

        const auto f1 = makeFrame(0x01, sensorPayload(4, 5.0f, 6.0f));
        p.feed(f1.data(), f1.size());

        assert(got.size() == 1);
    }

    // Case 5: CRC-corrupted frame, then a valid frame -> resync and recover.
    {
        std::vector<protocol::Frame> got;
        FrameParser p([&](const protocol::Frame &f) { got.push_back(f); });

        auto bad = makeFrame(0x01, sensorPayload(5, 7.0f, 8.0f));
        bad.back() ^= 0xFF;
        p.feed(bad.data(), bad.size());
        assert(got.empty());
        assert(p.stats().crcErrors == 1);

        const auto good = makeFrame(0x01, sensorPayload(6, 9.0f, 10.0f));
        p.feed(good.data(), good.size());
        assert(got.size() == 1);
        assert(p.stats().framesOk == 1);
    }

    // Case 6: impossible LEN (0xFFFF) -> immediate resync, next frame still parsed.
    {
        std::vector<protocol::Frame> got;
        FrameParser p([&](const protocol::Frame &f) { got.push_back(f); });

        const uint8_t badHeader[] = {0xAA, 0x55, 0x01, 0x01, 0xFF, 0xFF};
        p.feed(badHeader, sizeof(badHeader));
        assert(p.stats().resyncs == 1);

        const auto good = makeFrame(0x06, {});
        p.feed(good.data(), good.size());
        assert(got.size() == 1);
    }

    std::printf("test_parser: PASS (6 cases)\n");
    return 0;
}
