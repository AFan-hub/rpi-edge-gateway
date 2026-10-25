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

## Architecture

```text
             ┌──────────────────────────── Raspberry Pi 5 ─────────────────────────────┐
             │                                                                         │
 /dev/ttyUSB0│  SerialRxThread            ProcessThread                 NetworkThread │
 STM32 ──────►  termios nonblocking ─┐    IIR-free passthrough ─┐        epoll loop   │
 (AA55 frames)│  read() chunk feed   │    DataProcessor        │        listen+accept│
             │        │             │        │                │        client buffers│
             │        ▼             │        ▼                │            │        │
             │  FrameParser ──ok──► ThreadSafeQueue ──► TcpServer::push ──► 3 clients │
             │  (half/sticky/CRC/      (Frame)         (eventfd wake,   │   (PC test)  │
             │   resync, LEN cap)                     per-client out)    │             │
             │        │                                                      │         │
             │        └── stats ──► MonitorThread: fps / crc_err / queue / clients     │
             └─────────────────────────────────────────────────────────────────────────┘
```

## Progress

- [x] Day 8-10: CMake, SerialPort(termios), Protocol+CRC16, FrameParser + tests
- [x] Day 11: threads wired to real lifecycle (rx/process in main.cpp)
- [x] Day 12: epoll TcpServer with multi-client broadcast (3-client acceptance PASS)
- [x] Day 13: Monitor statistics line (fps / crc errors / queue depth / clients)
- [x] Day 14: systemd deploy scripts + GDB/Valgrind/strace/perf records
  (see docs/debug_records.md)
- [x] Day 15-16: real-link integration - STM32 frames (10 Hz, crc_err=0
  over 1751 frames) through /dev/ttyUSB0 to TCP; 3 concurrent clients PASS
- [x] Day 17: fault injection - USB unplug/replug recovers in-process
  (same PID, stable by-id path), client drop/reconnect, systemd restart
- [ ] Day 18: 100 Hz accelerated stress (deferred; 10 Hz baseline and
  multi-client runs recorded in docs/debug_records.md)
