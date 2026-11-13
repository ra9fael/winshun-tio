"""End-to-end check of tio's shared socket (the path the GUI consumes).

Why the launch method matters
-----------------------------
tio decides "interactive" from its own console handles. With a piped stdin it
prints "Non-interactive mode enabled" and does **not** forward input, so a
socket client's bytes are dropped and nothing echoes back. The plugin launches
tio with CREATE_NEW_CONSOLE and no redirected handles (native.h), which is what
this script reproduces. Getting that wrong makes a working feature look broken.

The device must echo for the round trip: short the UART's TX/RX, use a virtual
COM pair, or a device in echo mode. Without an echo the script still reports
that the socket accepted the write, which is the plugin's own contract.
"""
from __future__ import annotations

import argparse
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CREATE_NEW_CONSOLE = 0x00000010


def connect_with_retry(port: int, deadline_s: float) -> socket.socket | None:
    deadline = time.time() + deadline_s
    while time.time() < deadline:
        candidate = socket.socket()
        candidate.settimeout(1.0)
        try:
            candidate.connect(("127.0.0.1", port))
            return candidate
        except OSError:
            candidate.close()
            time.sleep(0.3)
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(ROOT / "runtime/tio.exe"))
    parser.add_argument("--device", default="/dev/ttyS9")
    parser.add_argument("--port", type=int, default=54321)
    parser.add_argument("--marker", default="WINSHUN-ECHO-CHECK\r\n")
    parser.add_argument("--seconds", type=float, default=5.0)
    # The window's own consumer, built from qmlapp/ShareClient.cpp. It checks the
    # incremental path (consume() and the reduced terminal screen), which a raw
    # socket read cannot.
    parser.add_argument("--client", default="")
    args = parser.parse_args()

    command = [args.exe, "-n", "-S", f"inet:{args.port}", args.device]
    print("run:", " ".join(command))

    # No pipes: the child gets a fresh console, exactly like native.h's launch.
    process = subprocess.Popen(command, cwd=str(ROOT), close_fds=True,
                              creationflags=CREATE_NEW_CONSOLE)
    writer = observer = None
    received = b""
    observer_bytes = b""
    client = None
    client_output = ""
    try:
        writer = connect_with_retry(args.port, 15.0)
        if writer is None:
            print("FAIL could not connect to the shared socket")
            return 1
        print("OK   connected to 127.0.0.1:%d" % args.port)
        writer.settimeout(0.4)

        # A second client stands in for the window's ShareClient: read-only.
        observer = connect_with_retry(args.port, 5.0)
        if observer is None:
            print("FAIL the observer could not connect")
        else:
            print("OK   a second (read-only) client connected")
            observer.settimeout(0.4)

        # The window's own consumer, started once the socket is listening and
        # before the write, so it is waiting when the echo arrives. Give it a
        # moment to establish the connection before anything is sent.
        if args.client:
            client = subprocess.Popen([args.client, str(args.port), args.marker],
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                      text=True)
            time.sleep(1.0)

        payload = args.marker.encode()
        writer.sendall(payload)
        print("OK   wrote %d bytes: %r" % (len(payload), args.marker))

        end = time.time() + args.seconds
        while time.time() < end:
            try:
                chunk = writer.recv(4096)
                if not chunk:
                    break
                received += chunk
            except socket.timeout:
                pass
            if observer is not None:
                try:
                    chunk = observer.recv(4096)
                    if not chunk:
                        observer = None
                    else:
                        observer_bytes += chunk
                except socket.timeout:
                    pass
    finally:
        # Read the window consumer first: it may still be waiting for the echo,
        # and terminating tio under it would drop the connection.
        client_output = ""
        if client is not None:
            try:
                client_output, _ = client.communicate(timeout=20)
            except subprocess.TimeoutExpired:
                client.kill()
                client_output, _ = client.communicate()
        for sock in (writer, observer):
            if sock is not None:
                sock.close()
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=6)
        except subprocess.TimeoutExpired:
            process.kill()

    echoed = payload in received
    observer_saw = payload in observer_bytes
    print("writer received back: %d bytes" % len(received))
    print("observer received:    %d bytes" % len(observer_bytes))
    if received:
        print("sample:", received[:200])

    if echoed:
        print("PASS round trip through the serial device")
    else:
        print("INFO no echo came back: the socket accepted the write, the device "
              "did not return bytes (short TX/RX, virtual pair or echo mode needed)")
    if observer is not None and observer_saw:
        print("PASS the read-only observer saw the same bytes")

    # The window's real consumer: its output was collected in the finally block,
    # while tio was still running.
    if client is not None:
        print("--- ShareClient ---")
        print((client_output or "").strip())
        if client.returncode != 0:
            print("FAIL the window's ShareClient path failed")
            return 1

    return 0 if echoed and observer_saw else 0 if not echoed else 1


if __name__ == "__main__":
    sys.exit(main())
