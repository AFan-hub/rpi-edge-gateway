#!/usr/bin/env bash
# Hardware-free end-to-end smoke test:
#   socat pty pair  ->  edge_gateway (reader)  +  serial_simulator.py (writer)
#
# Runs entirely inside one session (WSL tears the VM down between
# invocations, so never orchestrate these pieces from separate shells).
#
# Usage: bash scripts/smoke_test.sh [frames] [corrupt_rate]
set -u
cd "$(dirname "$0")/.."

FRAMES="${1:-200}"
CORRUPT="${2:-0.02}"

pkill -x socat          2>/dev/null
pkill -x edge_gateway   2>/dev/null
pkill -f "[s]erial_simulator.py" 2>/dev/null
sleep 0.5

socat pty,raw,echo=0,link=/tmp/vport0,mode=666 \
      pty,raw,echo=0,link=/tmp/vport1,mode=666 >/tmp/socat.log 2>&1 &
socat_pid=$!
sleep 1

./build/edge_gateway /tmp/vport0 >/tmp/gw.log 2>&1 &
gw_pid=$!
sleep 1

if ! kill -0 "$gw_pid" 2>/dev/null; then
    echo "FAIL: gateway did not start"
    cat /tmp/gw.log
    kill "$socat_pid" 2>/dev/null
    exit 1
fi

python3 scripts/serial_simulator.py /tmp/vport1 --frames "$FRAMES" --corrupt "$CORRUPT"
sleep 1

kill -INT "$gw_pid"
for _ in $(seq 1 20); do
    kill -0 "$gw_pid" 2>/dev/null || break
    sleep 0.5
done
if kill -0 "$gw_pid" 2>/dev/null; then
    echo "FAIL: gateway did not exit on SIGINT"
    kill -9 "$gw_pid"
fi

echo "--- gateway log (head/tail):"
head -2 /tmp/gw.log
tail -3 /tmp/gw.log

kill "$socat_pid" 2>/dev/null
wait "$socat_pid" 2>/dev/null
echo "--- smoke test done"
