# Day 19 代码质量自查（Linux / C++）

按手册 Day 19 清单检查，结论：**通过**，附已知限制。

## 1. magic number

| 位置 | 处理 |
|---|---|
| 协议常量 | `Protocol.hpp` `constexpr`，与 STM32 `protocol.h` 一一对应 |
| epoll 参数、缓冲上限 | `TcpServer.cpp` 匿名命名空间 `kMaxClients/kClientBufCap/kBroadcastCap/kMaxEvents` |
| 默认设备/波特率/端口 | `main.cpp` argv 默认值（可覆盖），接受 |
| 环形缓冲/日志格式字符串 | 字面量集中且单处出现，接受 |

## 2. const / 类型

- 协议结构/帧全 `const` 引用传递；编解码入参 `const uint8_t*`
- `constexpr` 用于协议与容类别常量
- `size_t`/`ssize_t` 与系统 API 类型一致；格式化输出用显式 cast

## 3. 错误处理

| 路径 | 处理 |
|---|---|
| socket/bind/listen/epoll/eventfd | 失败返回 false，日志带 `strerror(errno)`，进程降级（日志-only）或退出 |
| serial 读错误 | errno 分类：EAGAIN=空闲；其他=关闭重连 |
| 设备消失 | 空闲>2s 且 by-id 节点不存在 → 关闭 → 每秒重试（同 PID 自愈） |
| TCP 部分写/慢客户端 | 每客户端待发缓冲，64KB 上限，超限断流计数 |
| 客户端闪断 | epoll 事件驱动清理，不影响其他客户端 |

## 4. 生命周期 / RAII

- `SerialPort` 析构 close；`TcpServer` 析构 stop
- 锁全部 RAII（`lock_guard`/`unique_lock` + predicate）
- 无线程泄漏：四个线程全部 join；SIGINT/SIGTERM 走统一停止路径
- `ThreadSafeQueue::stop()` + `notify_all` 保证阻塞线程可退出

## 5. 线程安全

- `clients_` 由 `clientsMutex_` 保护（事件循环与外部查询/停止路径）
- 广播队列 `queueMutex_` + eventfd 唤醒，生产者不直接触碰 epoll 状态
- `FrameParser::Stats` 为原子计数，监控线程无锁读取

## 6. include 层级

- 公共头只暴露必要类型；系统头在 .cpp
- `gateway_core`（CRC/协议/解析/日志）不依赖 POSIX，可单独在宿主机跑单测（ctest 2/2）

## 已知限制（记录，不修）

1. TCP 转发为文本行（`[data] ...`）；二进制帧透传留作扩展（协议与帧解析均在树内，成本较低）
2. 无客户端时广播数据直接丢弃（by design，不缓存历史）
3. 设备路径固定为 CH340 的 by-id 名；换用其他 USB-UART 需改 unit 或参数（可用配置文件替代，留作扩展）
4. 100 Hz 加速压测未执行；10 Hz 真机 + 三客户端并发 + perf 0.7% CPU 已记录
