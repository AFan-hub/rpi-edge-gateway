#ifndef CRC16_HPP
#define CRC16_HPP

#include <cstddef>
#include <cstdint>

namespace crc
{
// CRC16-CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout.
// MUST stay byte-identical to Components/crc16.c on the STM32 side.
// Check value: ccittFalse("123456789", 9) == 0x29B1
uint16_t ccittFalse(const uint8_t *data, size_t len);

} // namespace crc

#endif
