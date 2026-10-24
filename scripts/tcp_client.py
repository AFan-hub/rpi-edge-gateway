#!/usr/bin/env python3
"""Minimal TCP client for the gateway. Usage: tcp_client.py <host> [port]"""
import socket
import sys

host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

with socket.create_connection((host, port)) as s:
    while True:
        data = s.recv(4096)
        if not data:
            break
        print(data.hex(" "))
