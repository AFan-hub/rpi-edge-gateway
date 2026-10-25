# rpi_edge_gateway — 树莓派 5 + Linux C++ 嵌入式边缘数据网关

从 STM32 接收二进制帧（`AA 55 VER CMD LEN PAYLOAD CRC16`），流式解析
（半包/粘包/CRC 错误重同步/LEN 上限），经线程安全队列解耦，并由
epoll + 非阻塞 Socket 广播给多个 TCP 客户端。含 systemd 部署、
运行统计与完整的调试记录。

## Architecture

```text
             ┌──────────────────────────── Raspberry Pi 5 ─────────────────────────────┐
             │                                                                         │
 /dev/ttyUSB0│  SerialRxThread            ProcessThread                 NetworkThread │
 STM32 ──────►  termios nonblocking ─┐    decode + text line ─┐        epoll loop   │
 (AA55 frames)│  read() chunk feed   │    DataProcessor sink │        listen+accept│
             │        │             │        │                │        client buffers│
             │        ▼             │        ▼                │            │        │
             │  FrameParser ──ok──► ThreadSafeQueue ──► TcpServer::push ──► N clients │
             │  (half/sticky/CRC/      (Frame)        (eventfd wake, per-client out) │
             │   resync, LEN cap)                                                  │
             │        └── stats ──► MonitorThread: fps / crc_err / resync / queue / clients │
             └─────────────────────────────────────────────────────────────────────────┘
```

## Build / test

```bash
cmake -S . -B build && cmake --build build -j4
cd build && ctest --output-on-failure     # protocol + parser (6 cases)
```

## Run

```bash
./build/edge_gateway [device] [baud] [tcp_port]     # defaults: /dev/ttyUSB0 115200 9000
```

Connect clients: `python3 scripts/tcp_client.py <host> 9000 --text`

Hardware-less smoke test: `scripts/smoke_test.sh`（socat 虚拟串口 + 模拟器）;
debug capture: `scripts/collect_debug.sh`（valgrind/strace/gdb/perf）;
multi-client acceptance: `scripts/accept_tcp.py host port seconds clients`.

## Protocol

```
AA 55 VER CMD LEN_L LEN_H PAYLOAD CRC_L CRC_H   (little-endian, CRC16-CCITT-FALSE over VER..PAYLOAD)
CMD 0x01 SENSOR_DATA -> uint32 ts_ms; float s1; float s2; uint16 status  (14 bytes)
Check vector: CRC16("123456789") == 0x29B1
```

## Deployment (systemd)

```bash
sudo bash scripts/deploy.sh          # installs /opt/edge_gateway + unit (by-id device path)
sudo bash scripts/staging_demo.sh    # hardware-less acceptance via socat drop-in
```

`Restart=on-failure` + `RestartSec=2`；串口热插拔由进程内重连处理
（同 PID，by-id 稳定路径）。详见 `docs/debug_records.md`。

## Progress

- [x] Day 8-10: CMake, SerialPort(termios), Protocol+CRC16, FrameParser + tests
- [x] Day 11: rx/process threads with graceful SIGINT
- [x] Day 12: epoll TcpServer, multi-client broadcast (3-client PASS)
- [x] Day 13: monitor statistics line (fps/crc/queue/clients)
- [x] Day 14: systemd + valgrind/strace/gdb/perf records
- [x] Day 15-16: real-link integration with STM32 (10 Hz, crc_err=0 / 1751 frames; 3 clients PASS)
- [x] Day 17: fault injection — unplug/replug in-process recovery (same PID),
      client drop/reconnect, systemd auto-restart verified
- [ ] Day 18: 100 Hz accelerated stress — not executed, explicitly not claimed

## Known limitations

- TCP 转发为文本行；二进制帧透传为可扩展项
- 无客户端时广播数据不缓存（by design）
- 设备路径固定为 CH340 by-id 名
