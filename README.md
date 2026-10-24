# rpi_edge_gateway

Raspberry Pi 5 + Linux C++17 embedded edge data gateway.
Receives binary frames from an STM32 over UART, parses them stream-wise
(half packets / sticky packets / CRC errors / resync), and forwards decoded
data to TCP clients via epoll.

## Layout

- `include/` `src/` — gateway sources; `gateway_core` (CRC, protocol, parser,
  logger) is platform-independent and shared with the unit tests
- `tests/` — host-runnable regression tests (`ctest`)
- `scripts/` — `serial_simulator.py` (frame source with split/merge/corrupt),
  `tcp_client.py`
- `systemd/edge_gateway.service` — deployment unit

## Build & test

```bash
mkdir -p build && cd build
cmake .. && make -j4
ctest --output-on-failure
```

## Run against the simulator (no hardware needed)

```bash
socat -d -d pty,raw,echo=0 pty,raw,echo=0
./build/edge_gateway /dev/pts/N &          # N = first pty printed
python3 scripts/serial_simulator.py /dev/pts/M --frames 300 --corrupt 0.02
```

## Progress

- [x] Day 8-10: CMake, SerialPort(termios), Protocol+CRC16, FrameParser + tests
- [ ] Day 11: threads wired to real lifecycle (rx/process already in main.cpp)
- [ ] Day 12: TcpServer epoll implementation (currently a stub)
- [ ] Day 13: Logger/Monitor statistics (RX FPS, CRC errors, queue depth)
- [ ] Day 14: systemd deploy + GDB/Valgrind/strace/perf session
