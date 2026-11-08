#!/usr/bin/env python3
"""
Wireless FBI Remote CIA Installer for New Nintendo 2DS XL / 3DS
Based on Steveice10/FBI servefiles.py protocol (TCP Port 5000 + Local HTTP Server)

Supports two wireless installation modes without ever removing your microSD card:
1. Automatic Push Mode ("Receive URLs over the network" in FBI):
   - On your 2DS XL: Open FBI -> Remote Install -> Receive URLs over the network
   - Run: python3 tools/remote_install_cia.py <2DS_IP_ADDRESS> exo.cia
2. Manual URL Mode ("Manually enter URLs" in FBI):
   - Run: python3 tools/remote_install_cia.py --serve-only exo.cia
   - On your 2DS XL: Open FBI -> Remote Install -> Manually enter URLs
   - Enter the short URL printed in your terminal (e.g. 192.168.1.50:8080/exo.cia)
"""

import os
import sys
import time
import struct
import socket
import threading
from http.server import SimpleHTTPRequestHandler
from socketserver import TCPServer
from urllib.parse import quote


def detect_local_ip(target_ip=None):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe_ip = target_ip if (target_ip and not target_ip.startswith("-")) else "8.8.8.8"
        s.connect((probe_ip, 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = "127.0.0.1"
    finally:
        s.close()
    return ip


class ReusableTCPServer(TCPServer):
    allow_reuse_address = True


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__.strip())
        sys.exit(0)

    serve_only = False
    if sys.argv[1] == "--serve-only":
        serve_only = True
        cia_path = sys.argv[2] if len(sys.argv) >= 3 else "exo.cia"
        target_ip = None
        host_port = int(sys.argv[3]) if len(sys.argv) >= 4 else 8080
    elif len(sys.argv) == 2 and sys.argv[1].endswith((".cia", ".3dsx")):
        cia_path = sys.argv[1]
        target_ip = input("Enter the IP address shown on your 2DS XL FBI screen: ").strip()
        host_port = 8080
    else:
        target_ip = sys.argv[1].strip()
        cia_path = sys.argv[2].strip() if len(sys.argv) >= 3 else "exo.cia"
        host_port = int(sys.argv[3]) if len(sys.argv) >= 4 else 8080

    cia_path = os.path.abspath(cia_path)
    if not os.path.isfile(cia_path):
        print(f"Error: File not found: {cia_path}")
        print("Run `make cia` first (or use GitHub Actions) to build exo.cia.")
        sys.exit(1)

    host_ip = detect_local_ip(target_ip)
    directory = os.path.dirname(cia_path)
    filename = os.path.basename(cia_path)
    os.chdir(directory)

    url_payload = f"{host_ip}:{host_port}/{quote(filename)}"
    payload_bytes = url_payload.encode("ascii")

    server = ReusableTCPServer(("", host_port), SimpleHTTPRequestHandler)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()

    print(f"\n[+] Started HTTP server on {host_ip}:{host_port}")
    print(f"[+] Serving CIA URL: http://{url_payload}\n")

    if serve_only or not target_ip:
        print("=== MANUAL URL MODE ===")
        print("1. On your 2DS XL, open FBI -> Remote Install -> Manually enter URLs")
        print(f"2. Type: {url_payload}")
        print("3. Press Ctrl+C here once installation finishes on your 2DS XL.")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\nShutting down HTTP server...")
            server.shutdown()
            return

    print(f"[+] Connecting to 2DS XL FBI at {target_ip}:5000...")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(15)
        sock.connect((target_ip, 5000))
        # FBI expects a big-endian uint32 length followed by ASCII URL string(s)
        packet = struct.pack("!L", len(payload_bytes)) + payload_bytes
        sock.sendall(packet)
        print("[+] URL sent! Look at your 2DS XL and press (A) to confirm installation...")
        sock.settimeout(None)
        while len(sock.recv(1)) < 1:
            time.sleep(0.05)
        sock.close()
        print("[+] Installation complete!")
    except Exception as exc:
        print(f"[!] Could not push automatically to {target_ip}:5000 ({exc}).")
        print(f"    Make sure FBI is on 'Remote Install -> Receive URLs over the network',")
        print(f"    or select 'Manually enter URLs' in FBI and enter: {url_payload}")
        print("    Keeping HTTP server open for 120 seconds (Ctrl+C to stop)...")
        try:
            time.sleep(120)
        except KeyboardInterrupt:
            pass
    finally:
        server.shutdown()


if __name__ == "__main__":
    main()
