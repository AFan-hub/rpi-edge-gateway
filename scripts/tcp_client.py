#!/usr/bin/env python3
"""Minimal TCP client for the gateway.

Usage: tcp_client.py <host> [port] [--text]
       --text prints the ASCII lines; default is a hex dump.
"""
import socket
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
text_mode = "--text" in sys.argv

host = args[0] if len(args) > 0 else "127.0.0.1"
port = int(args[1]) if len(args) > 1 else 9000

with socket.create_connection((host, port)) as s:
    while True:
        data = s.recv(4096)
        if not data:
            break
        if text_mode:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()
        else:
            print(data.hex(" "))
