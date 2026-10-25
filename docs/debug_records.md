# 调试与运行记录（Day 12-14）

> 复现：`bash scripts/collect_debug.sh [秒]`（需要 socat/valgrind/strace/gdb；
> perf 在树莓派上用 `/usr/lib/linux-tools/6.8.0-1065-raspi/perf`）。
> 数据源：socat 虚拟串口对 + scripts/serial_simulator.py。

## Day 12：三客户端并发验收（树莓派 5，Ubuntu 24.04 aarch64）

```
client 0: lines=159 bytes=7375 (ok)
client 1: lines=159 bytes=7375 (ok)
client 2: lines=159 bytes=7375 (ok)
VERDICT: PASS
[shutdown] frames_ok=180 crc_errors=0 resyncs=0 processed=180
           tcp_pushed=8349 tcp_dropped_clients=0
```

日志同时显示 `client connected, fd=7/8/9 total=3` 与断开后 `remaining=0`，
慢客户端策略未触发。

## Day 13：监控行（WSL Ubuntu 22.04 x86_64 对照运行）

```
[monitor] fps=17.4 frames=87 crc_err=1 resync=0 queue=0 clients=0 processed=87
[shutdown] frames_ok=119 crc_errors=1 resyncs=0 processed=119 tcp_pushed=5483
```

## Day 14：Valgrind（WSL 对照）

```
in use at exit: 0 bytes in 0 blocks
total heap usage: 339 allocs, 339 frees, 89,438 bytes allocated
All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts
```

## Day 14：strace 系统调用画像（WSL 对照，epoll 等待/读/写主导）

```
epoll_wait 73      read 4958      write 149
socket 1  bind 1   listen 1       accept4 0(该轮无客户端)
fcntl 2  eventfd2 1
epoll_wait(5, <unfinished ...>
<... epoll_wait resumed>[{events=EPOLLIN, data={u32=6, u64=6}}], 32, -1) = 1
```

## Day 14：GDB（断点 + 全线程栈）

```
Thread 4 "edge_gateway" hit Breakpoint 1 in DataProcessor::handle(Frame const&)
Thread 4: #0 DataProcessor::handle  #1 main::{lambda()#4} ...   ← ProcessThread
Thread 5: __clock_nanosleep ← sleep_for<ms> ← main::{lambda()#5} ← MonitorThread
Thread 3: __clock_nanosleep ← sleep_for<ms> ← main::{lambda()#3} ← RX 轮询线程
```

## Day 14：perf（待树莓派在线补录）

WSL 无 perf；Pi 上执行：
`/usr/lib/linux-tools/6.8.0-1065-raspi/perf stat -e task-clock,cycles,instructions ./build/edge_gateway /tmp/vp0`

## Day 14：systemd（待树莓派在线补录）

在 Pi 上：
```
sudo bash scripts/deploy.sh        # 安装到 /opt/edge_gateway 并启动
sudo bash scripts/staging_demo.sh  # 无硬件演示：socat 虚拟口 + 自动重启验证
```
验收点：`systemctl status` 显示 active；`kill -9` 后 `Restart=on-failure`
自动拉起；`journalctl -u edge_gateway` 可见 `[monitor]` 统计行。
