#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "Crc16.hpp"
#include "Protocol.hpp"

int main()
{
    // Check value: CRC16-CCITT-FALSE("123456789") == 0x29B1.
    // The STM32 C implementation is verified against the same vector,
    // which is what keeps the two ends byte-compatible.
    const std::string check = "123456789";
    assert(crc::ccittFalse(reinterpret_cast<const uint8_t *>(check.data()),
                           check.size()) == 0x29B1);

    protocol::SensorPayload sensor{};
    sensor.timestamp_ms = 12345;
    sensor.sensor_1     = 25.5f;
    sensor.sensor_2     = -1.25f;
    sensor.status       = 0x0001;

    uint8_t buf[512];
    const size_t n = protocol::encodeSensor(sensor, buf, sizeof(buf));

    assert(n == protocol::HEADER_SIZE + protocol::SENSOR_PAYLOAD_SIZE +
                  protocol::CRC_SIZE);
    assert(buf[0] == 0xAA && buf[1] == 0x55);
    assert(buf[2] == protocol::VERSION);
    assert(buf[3] == 0x01);              // CMD_SENSOR_DATA
    assert(buf[4] == 14 && buf[5] == 0); // LEN little-endian

    // CRC is little-endian and covers VER .. end of PAYLOAD.
    const uint16_t expect = crc::ccittFalse(buf + 2, n - 4);
    assert(static_cast<uint16_t>(buf[n - 2] | (buf[n - 1] << 8)) == expect);

    // Decode round trip.
    protocol::Frame frame;
    frame.cmd = protocol::Command::SensorData;
    frame.payload.assign(buf + protocol::HEADER_SIZE,
                         buf + protocol::HEADER_SIZE + 14);

    protocol::SensorPayload out{};
    assert(protocol::decodeSensor(frame, out));
    assert(out.timestamp_ms == 12345);
    assert(out.sensor_1 == 25.5f);
    assert(out.sensor_2 == -1.25f);
    assert(out.status == 1);

    // Oversized payload must be rejected, not truncated.
    uint8_t big[protocol::MAX_PAYLOAD + 1] = {0};
    assert(protocol::encode(protocol::Command::Heartbeat, big,
                            sizeof(big), buf, sizeof(buf)) == 0);

    std::printf("test_protocol: PASS\n");
    return 0;
}
