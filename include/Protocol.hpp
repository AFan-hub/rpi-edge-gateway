#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace protocol
{

constexpr uint8_t  HEAD1       = 0xAA;
constexpr uint8_t  HEAD2       = 0x55;
constexpr uint8_t  VERSION     = 0x01;
constexpr uint16_t MAX_PAYLOAD = 256;
constexpr size_t   HEADER_SIZE = 6; // HEAD1 HEAD2 VER CMD LEN_L LEN_H
constexpr size_t   CRC_SIZE    = 2;

enum class Command : uint8_t
{
    SensorData  = 0x01,
    SystemState = 0x02,
    SetConfig   = 0x03,
    GetConfig   = 0x04,
    Ack         = 0x05,
    Heartbeat   = 0x06,
    Error       = 0x07,
};

// Wire layout, all multi-byte fields little-endian:
// +-------+-------+-----+-----+--------+---------+--------+
// | 0xAA  | 0x55  | VER | CMD | LEN(2) | PAYLOAD | CRC(2) |
// +-------+-------+-----+-----+--------+---------+--------+
// CRC16-CCITT-FALSE is computed over VER .. end of PAYLOAD (not the head).
struct Frame
{
    uint8_t version = VERSION;
    Command cmd = Command::Heartbeat;
    std::vector<uint8_t> payload;
};

// Field order/size match SensorPayload_t on the STM32 side (packed, 14 bytes).
// Both ends are little-endian ARM, so raw memcpy is acceptable for v1;
// a stricter cross-platform protocol would serialize field by field.
//
// NOTE: the wire size is SENSOR_PAYLOAD_SIZE, NOT sizeof(SensorPayload):
// the unpacked host struct carries tail padding (16 bytes). Using sizeof()
// here is exactly the cross-endian bug the unit test caught on Day 0.
struct SensorPayload
{
    uint32_t timestamp_ms;
    float    sensor_1;
    float    sensor_2;
    uint16_t status;
};

constexpr size_t SENSOR_PAYLOAD_SIZE = 14; // 4 + 4 + 4 + 2, no padding

// Returns the encoded frame length, or 0 if payloadLen > MAX_PAYLOAD
// or outSize is too small.
size_t encode(Command cmd,
              const uint8_t *payload,
              uint16_t payloadLen,
              uint8_t *out,
              size_t outSize);

size_t encodeSensor(const SensorPayload &data,
                    uint8_t *out,
                    size_t outSize);

bool decodeSensor(const Frame &frame, SensorPayload &out);

} // namespace protocol

#endif
