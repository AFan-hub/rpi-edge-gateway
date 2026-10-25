#!/usr/bin/env bash
# Day-14 debug capture: drives the gateway with a socat virtual serial pair
# and collects valgrind / strace / gdb / perf records.
#
# Usage: bash scripts/collect_debug.sh [seconds]   (default 6)
# Requires: built gateway, socat, valgrind, strace, gdb.
# perf is optional; on the Pi use the full path shown in the output.
# Everything (pty links + logs) lives in a private temp dir, so the script
# is safe to run as root (/tmp files owned by other users are protected).
set -u
cd "$(dirname "$0")/.."
SECS=${1:-6}
BIN=./build/edge_gateway

if [ ! -f "$BIN" ]; then
    echo "build first: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

chmod +x "$BIN"   # tar copies from Windows can drop the exec bit

LOGDIR=$(mktemp -d /tmp/gw_dbg.XXXXXX)
VP0=$LOGDIR/vp0
VP1=$LOGDIR/vp1

cleanup() { pkill -x socat 2>/dev/null; pkill -x edge_gateway 2>/dev/null; }

pair_up() {
    cleanup; sleep 0.2
    socat pty,raw,echo=0,link=$VP0,mode=666 \
          pty,raw,echo=0,link=$VP1,mode=666 \
          </dev/null >$LOGDIR/socat.log 2>&1 &
    sleep 0.6
}

feed() {
    nohup python3 scripts/serial_simulator.py $VP1 \
          --frames $((SECS * 12)) --corrupt 0 </dev/null >$LOGDIR/sim.log 2>&1 &
}

echo "logs: $LOGDIR"
echo "===== VALGRIND ====="
pair_up; feed
timeout -s INT "$SECS" valgrind --leak-check=full --show-leak-kinds=all \
        "$BIN" $VP0 115200 9000 >$LOGDIR/vg.log 2>&1
grep -E "in use at exit|total heap usage|All heap blocks|ERROR SUMMARY" $LOGDIR/vg.log

echo
echo "===== STRACE ====="
pair_up; feed
timeout -s INT "$SECS" strace -f -o $LOGDIR/trace.log \
        "$BIN" $VP0 115200 9000 >/dev/null 2>&1
for s in epoll_wait epoll_pwait2 epoll_pwait epoll_ctl read write socket bind listen accept4 fcntl eventfd2; do
    printf '%-12s %s\n' "$s" "$(grep -c " $s(" $LOGDIR/trace.log)"
done
echo "-- epoll_pwait2 sample:"
grep -E 'epoll_(wait|pwait)' $LOGDIR/trace.log | head -2

echo
echo "===== GDB ====="
pair_up; feed
gdb -batch -ex "set confirm off" \
    -ex "break DataProcessor::handle" \
    -ex "run" \
    -ex "info threads" \
    -ex "thread apply all bt" \
    --args "$BIN" $VP0 115200 9000 >$LOGDIR/gdb.log 2>&1
grep -E "hit Breakpoint|^Thread|^#[0-9]+ " $LOGDIR/gdb.log | head -24

echo
echo "===== PERF ====="
PERF=""
for p in /usr/lib/linux-tools/*/perf; do     # prefer a real binary over the
    [ -x "$p" ] && PERF="$p" && break        # kernel-version wrapper
done
[ -n "$PERF" ] || PERF=$(command -v perf || true)
if [ -n "$PERF" ]; then
    pair_up; feed
    timeout -s INT "$SECS" "$PERF" stat -e task-clock,cycles,instructions \
            "$BIN" $VP0 115200 9000 >$LOGDIR/perf.log 2>&1
    tail -12 $LOGDIR/perf.log
else
    echo "perf not found (Pi ships it under /usr/lib/linux-tools/*/perf)"
fi

cleanup
