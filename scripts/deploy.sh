#!/usr/bin/env bash
# Install the edge gateway as a systemd service (run with sudo on the Pi):
#   sudo bash scripts/deploy.sh
set -euo pipefail
cd "$(dirname "$0")/.."

APP_DIR=/opt/edge_gateway

if [ ! -f build/edge_gateway ]; then
    echo "build/edge_gateway missing - run cmake build first" >&2
    exit 1
fi

# tar copies from Windows can drop the exec bit; harmless to re-assert it.
chmod +x build/edge_gateway

install -d "$APP_DIR/bin" "$APP_DIR/config"
install -m 0755 build/edge_gateway "$APP_DIR/bin/edge_gateway"
install -m 0644 systemd/edge_gateway.service /etc/systemd/system/edge_gateway.service

systemctl daemon-reload
systemctl enable edge_gateway
# clear a start-rate-limit lockout from earlier failed attempts
systemctl reset-failed edge_gateway 2>/dev/null || true
systemctl restart edge_gateway || true

sleep 1
systemctl --no-pager --full status edge_gateway | head -12 || true
echo
echo "logs: journalctl -u edge_gateway -f"
