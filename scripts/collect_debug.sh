#!/usr/bin/env bash
# Day-14 debug capture: drives the gateway with a socat virtual serial pair
# and collects valgrind / strace / gdb / perf records.
#
# Usage: bash scripts/collect_debug.sh [seconds]   (default 6)
# Requires: built gateway, socat, valgrind, strace, gdb.
# perf is optional; on the Pi use the full path shown in the output.
set -u
cd "$(dirname "$0")/.."
SECS=${1:-6}
BIN=./build/edge_gateway

if [ ! -x "$BIN" ]; then
    echo "build first: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

cleanup() { pkill -x socat 2>/dev/null; pkill -x edge_gateway 2>/dev/null; }

pair_up() {
    cleanup; sleep 0.2
    socat pty,raw,echo=0,link=/tmp/vp0,mode=666 \
          pty,raw,echo=0,link=/tmp/vp1,mode=666 \
          </dev/null >/dev/null 2>&1 &
    sleep 0.6
}

feed() {
    nohup python3 scripts/serial_simulator.py /tmp/vp1 \
          --frames $((SECS * 12)) --corrupt 0 </dev/null >/tmp/sim.log 2>&1 &
}

echo "===== VALGRIND ====="
pair_up; feed
timeout -s INT "$SECS" valgrind --leak-check=full --show-leak-kinds=all \
        "$BIN" /tmp/vp0 115200 9000 >/tmp/vg.log 2>&1
grep -E "in use at exit|total heap usage|All heap blocks|ERROR SUMMARY" /tmp/vg.log

echo
echo "===== STRACE ====="
pair_up; feed
timeout -s INT "$SECS" strace -f -o /tmp/trace.log \
        "$BIN" /tmp/vp0 115200 9000 >/dev/null 2>&1
for s in epoll_wait epoll_ctl read write socket bind listen accept4 fcntl eventfd2; do
    printf '%-10s %s\n' "$s" "$(grep -c " $s(" /tmp/trace.log)"
done
echo "-- epoll_wait sample:"
grep 'epoll_wait' /tmp/trace.log | head -2

echo
echo "===== GDB ====="
pair_up; feed
gdb -batch -ex "set confirm off" \
    -ex "break DataProcessor::handle" \
    -ex "run" \
    -ex "info threads" \
    -ex "thread apply all bt" \
    --args "$BIN" /tmp/vp0 115200 9000 >/tmp/gdb.log 2>&1
grep -E "^Thread|^#[0-9]+ " /tmp/gdb.log | head -30

echo
echo "===== PERF ====="
PERF=$(command -v perf || true)
if [ -z "$PERF" ] && [ -x /usr/lib/linux-tools/6.8.0-1065-raspi/perf ]; then
    PERF=/usr/lib/linux-tools/6.8.0-1065-raspi/perf
fi
if [ -n "$PERF" ]; then
    pair_up; feed
    timeout -s INT "$SECS" "$PERF" stat -e task-clock,cycles,instructions \
            "$BIN" /tmp/vp0 115200 9000 >/tmp/perf.log 2>&1
    tail -10 /tmp/perf.log
else
    echo "perf not found (Pi ships it under /usr/lib/linux-tools/*/perf)"
fi

cleanup
