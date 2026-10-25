# 调试与运行记录（Day 12-14）

环境：树莓派 5 / Ubuntu 24.04 aarch64 / kernel 6.8.0-1057-raspi /
g++ 13.3.0 / cmake 3.28 / valgrind 3.22 / gdb 15.1 /
perf 6.8.12（`/usr/lib/linux-tools/6.8.0-1065-raspi/perf`）。

复现：`sudo bash scripts/collect_debug.sh [秒]`——自动用 socat 虚拟串口对
驱动网关，采集 valgrind / strace / gdb / perf 四份记录；
数据源为 `scripts/serial_simulator.py`。

## Day 12：三客户端并发验收（Pi 原生 aarch64）

```
client 0: lines=159 bytes=7375 (ok)
client 1: lines=159 bytes=7375 (ok)
client 2: lines=159 bytes=7375 (ok)
VERDICT: PASS
[shutdown] frames_ok=180 crc_errors=0 resyncs=0 processed=180
           tcp_pushed=8349 tcp_dropped_clients=0
```

日志同时显示 `client connected, fd=7/8/9 total=3` 与断开后 `remaining=0`；
慢客户端背压策略未触发。

## Day 13：监控行（systemd journal 实录，喂 50 帧后）

```
[monitor] fps=10.0 frames=50 crc_err=0 resync=0 queue=0 clients=0 processed=50
```

（随后无数据流入时 fps 归 0、累计计数保持不变——统计口径正确。）

## Day 14：Valgrind（Pi 原生）

```
in use at exit: 0 bytes in 0 blocks
total heap usage: 326 allocs, 326 frees, 89,847 bytes allocated
All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts
```

## Day 14：strace 系统调用画像（Pi 原生）

```
epoll_pwait  73        epoll_ctl  2
read         5454      write      149
socket       1         bind       1     listen 1
accept4      0（该轮无客户端连接）
fcntl        2         eventfd2   1
```

样本：`epoll_pwait(5, <unfinished ...> ... [{events=EPOLLIN, data={u32=6,...}}], 32, -1, NULL, 8) = 1`
说明：该 glibc 实现走 `epoll_pwait`；事件循环阻塞在等待上，唤醒由
串口 fd 与 eventfd 驱动——与设计一致。

## Day 14：GDB（断点 + 全线程栈，Pi 原生 aarch64）

```
Thread 4 "edge_gateway" hit Breakpoint 1 in DataProcessor::handle(Frame const&)
Thread 5: __clock_nanosleep ← sleep_for<ms> ← main::{lambda()#3}  ← 监控线程
Thread 4: DataProcessor::handle ← main::{lambda()#2}              ← 处理线程
（另有 RX 轮询线程与 TCP 事件线程）
```

## Day 14：perf（Pi 原生，6 秒运行窗）

```
Performance counter stats (6.007 s elapsed):
     44.07 msec  task-clock   (0.007 CPUs utilized  = 0.7% CPU)
 73,831,853      cycles       (1.675 GHz)
144,102,822      instructions (1.95 insn per cycle)
     0.009 s user, 0.049 s sys
```

结论：网关在 10 Hz 数据流下 CPU 占用 **0.7%**——epoll + 每客户端缓冲 +
线程阻塞等待的设计把成本压到了噪声级。

## Day 14：systemd 部署与自动重启（Pi 原生）

- `sudo bash scripts/deploy.sh`：安装到 `/opt/edge_gateway`，`enabled`，
  `Restart=on-failure`、`RestartSec=2`
- 无硬件时用 `staging_demo.sh` 的 drop-in 把设备指向 socat 虚拟口；
  服务稳定 `active (running)`（5 线程，内存 ~408 KB）
- 喂 50 帧：journal 出现 `[data]` 行 + `[monitor] fps=10.0`
- `sudo pkill -9 -x edge_gateway`（故障终止）→ 3 秒后
  `Active: active (running) since ...; Main PID: 53433`（新 PID）——
  **自动重启验证通过**
- 语义备注：SIGTERM（优雅停）退出码 0，systemd 判定正常停止、不重启；
  SIGKILL 才走 on-failure——与设计一致

## Day 15-16：真机联调（STM32 → Pi → PC，2026-10-25）

链路：STM32F407 USART2(PA2/PA3) → CH340G → Pi `/dev/ttyUSB0` → epoll → TCP。

- STM32 固件 `[fw D3c]`：USART2 纯协议帧（日志镜像已退役）
- Pi journal（真实数据，10Hz）：
  ```
  ts=216607 s1=53.02 s2=14.33 status=0x0000
  [monitor] fps=10.0 frames=1751 crc_err=0 resync=0 queue=0 clients=1 processed=1751
  ```
  **1751 帧零 CRC 错、零重同步**；s1=光敏 48~57%，s2=片内温度 ~14°C（冷房间）
- PC 侧 TCP 客户端 `tcp_client.py --text`：连续 10Hz `[data]` 行，端到端闭环
- 三客户端并发（真实链路，8 秒）：各 81 行/3969 字节，`VERDICT: PASS`

## Day 17：故障注入（真实链路，2026-10-25）

### 拔插 USB-UART（进程内自愈）

```
[11423.379] [WARN ] serial device gone (/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0), closing for retry
[11428.379] [WARN ] waiting for serial device: /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
[11433.380] [WARN ] waiting for serial device: ...
[11436.396] [INFO ] serial device reopened: /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
[11436.482] [INFO ] ts=504807 s1=56.49 s2=12.01 status=0x0000   ← 数据立即恢复
```

- 断线 13 秒期间网关存活（monitor 持续 `fps=0.0`），**Main PID 82623 全程不变**
- 断线期间的 STM32 帧按设计丢弃（ts 489707 → 504807 跳变可证）

### 故障注入暴露并修复的三个真问题

1. `VMIN=0` 的 tty 上 `read()==0` 同时表示"空闲"与"拔线"，无法据此判 EOF
   → 改为**设备节点存在性探针**（空闲 >2 s 且 by-id 路径消失才判定拔线）
2. USB 重插后设备号漂移（ttyUSB0 → ttyUSB1）
   → systemd 单元改用稳定路径 `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`
3. 启动失败触发 systemd 启动熔断（`Start request repeated too quickly`）
   → deploy.sh 增加 `systemctl reset-failed`

### 客户端异常断开

```
[11706.582] [INFO ] client disconnected, fd=7 remaining=0
[11730.195] [INFO ] client connected, fd=7 total=1
```

客户端闪断后网关不重启、监听不中断，重连即恢复转发（epoll 多客户端语义）。

## Day 18：压力测试（部分执行）

- 10 Hz 真机连续运行：1751+ 帧零 CRC 错误、零重同步、队列零堆积、零丢帧
- 三客户端并发（真实链路）：完全同步，零丢
- CPU：perf 实测 0.7%（6 s 窗）
- **100 Hz 加速压测（10 ms 周期，~10k 帧）用户侧硬件拆装不便，未执行**，
  记录于此，不在简历/README 中声称该指标

## 真机部署切换（接 STM32 前）

```bash
sudo rm -rf /etc/systemd/system/edge_gateway.service.d   # 移除 staging 覆盖
sudo systemctl daemon-reload && sudo systemctl restart edge_gateway
```
