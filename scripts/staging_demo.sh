#!/usr/bin/env bash
# Hardware-less systemd acceptance on the Pi (run with sudo):
#   sudo bash scripts/staging_demo.sh
#
# Points the service at a socat virtual serial pair (/tmp/vp0) via a
# drop-in override, so Day-14 deployment/restart behaviour can be shown
# without the STM32 plugged in. Remove with:
#   sudo rm -rf /etc/systemd/system/edge_gateway.service.d && sudo systemctl daemon-reload
set -euo pipefail
cd "$(dirname "$0")/.."

install -d /etc/systemd/system/edge_gateway.service.d
cat > /etc/systemd/system/edge_gateway.service.d/override.conf <<'EOF'
[Service]
ExecStart=
ExecStart=/opt/edge_gateway/bin/edge_gateway /tmp/vp0 115200 9000
EOF

pkill -x socat 2>/dev/null || true
nohup socat pty,raw,echo=0,link=/tmp/vp0,mode=666 \
            pty,raw,echo=0,link=/tmp/vp1,mode=666 \
            </dev/null >/tmp/socat.log 2>&1 &

systemctl daemon-reload
systemctl restart edge_gateway
sleep 1
systemctl --no-pager --full status edge_gateway | head -12 || true

echo
echo "feed the gateway with:  python3 scripts/serial_simulator.py /tmp/vp1 --frames 50"
echo "watch the journal:      journalctl -u edge_gateway -f"
echo "restart demo:           sudo pkill -9 -x edge_gateway && sleep 3 && systemctl status edge_gateway"
