#!/usr/bin/env python3
"""Day-12 acceptance: N concurrent TCP clients against the gateway.

Usage: accept_tcp.py [host] [port] [seconds] [clients]

Connects `clients` sockets, each in its own thread, reads for `seconds`,
then prints per-client line/byte counts and a verdict.
"""
import socket
import sys
import threading

host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000
seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 8.0
nclients = int(sys.argv[4]) if len(sys.argv) > 4 else 3

results = []
lock = threading.Lock()


def worker(idx: int) -> None:
    lines = 0
    total = 0
    try:
        with socket.create_connection((host, port), timeout=3) as s:
            s.settimeout(0.5)
            stop_at = threading.Event()
            threading.Timer(seconds, stop_at.set).start()
            while not stop_at.is_set():
                try:
                    data = s.recv(4096)
                except socket.timeout:
                    continue
                if not data:
                    break
                total += len(data)
                lines += data.count(b"\n")
    except OSError as e:
        with lock:
            results.append((idx, -1, -1, str(e)))
        return
    with lock:
        results.append((idx, lines, total, "ok"))


threads = [threading.Thread(target=worker, args=(i,)) for i in range(nclients)]
for t in threads:
    t.start()
for t in threads:
    t.join()

ok = all(r[3] == "ok" and r[1] > 0 for r in results)
for idx, lines, total, status in sorted(results):
    print(f"client {idx}: lines={lines} bytes={total} ({status})")
print("VERDICT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
