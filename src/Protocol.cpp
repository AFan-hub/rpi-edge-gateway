#include "Protocol.hpp"

#include <cstring>

#include "Crc16.hpp"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "protocol v1 assumes a little-endian host"
#endif

namespace protocol
{

size_t encode(Command cmd,
              const uint8_t *payload,
              uint16_t payloadLen,
              uint8_t *out,
              size_t outSize)
{
    const size_t total = HEADER_SIZE + payloadLen + CRC_SIZE;

    if (payloadLen > MAX_PAYLOAD || outSize < total)
    {
        return 0;
    }

    out[0] = HEAD1;
    out[1] = HEAD2;
    out[2] = VERSION;
    out[3] = static_cast<uint8_t>(cmd);
    out[4] = static_cast<uint8_t>(payloadLen & 0xFF);
    out[5] = static_cast<uint8_t>(payloadLen >> 8);

    if (payloadLen > 0)
    {
        std::memcpy(out + HEADER_SIZE, payload, payloadLen);
    }

    const uint16_t crc = crc::ccittFalse(out + 2, 4 + payloadLen);
    out[total - 2] = static_cast<uint8_t>(crc & 0xFF);
    out[total - 1] = static_cast<uint8_t>(crc >> 8);

    return total;
}

size_t encodeSensor(const SensorPayload &data,
                    uint8_t *out,
                    size_t outSize)
{
    uint8_t payload[SENSOR_PAYLOAD_SIZE];

    std::memcpy(payload + 0,  &data.timestamp_ms, 4);
    std::memcpy(payload + 4,  &data.sensor_1,     4);
    std::memcpy(payload + 8,  &data.sensor_2,     4);
    std::memcpy(payload + 12, &data.status,       2);

    return encode(Command::SensorData, payload, SENSOR_PAYLOAD_SIZE, out, outSize);
}

bool decodeSensor(const Frame &frame, SensorPayload &out)
{
    if (frame.cmd != Command::SensorData ||
        frame.payload.size() != SENSOR_PAYLOAD_SIZE)
    {
        return false;
    }

    const uint8_t *p = frame.payload.data();

    std::memcpy(&out.timestamp_ms, p + 0,  4);
    std::memcpy(&out.sensor_1,     p + 4,  4);
    std::memcpy(&out.sensor_2,     p + 8,  4);
    std::memcpy(&out.status,       p + 12, 2);

    return true;
}

} // namespace protocol
