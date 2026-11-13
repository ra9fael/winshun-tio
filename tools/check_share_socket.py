"""Socket connectivity matrix for the packaged tio 3.9 (MSYS2 build).

Starts tio in each socket mode, polls the Windows TCP table and tries to
connect from a plain Winsock client, then reports exactly what happened.
Nothing is written to the serial device.
"""
from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "runtime" / "tio.exe"
DEVICE = "/dev/ttyS7"


def tcp_listeners(port: int) -> list[str]:
    """Ask Windows for LISTEN sockets on a port, via netstat (no PyWin32)."""
    try:
        out = subprocess.run(["netstat", "-ano", "-p", "TCP"], capture_output=True,
                             text=True, timeout=10).stdout
    except Exception as exc:  # noqa: BLE001
        return [f"netstat failed: {exc}"]
    rows = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        if parts[1].endswith(f":{port}") and "LISTEN" in line.upper():
            rows.append(line.strip())
    return rows


def try_connect(host: str, port: int, timeout: float = 1.0) -> str:
    family = socket.AF_INET6 if ":" in host else socket.AF_INET
    s = socket.socket(family, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((host, port))
        try:
            s.settimeout(1.5)
            data = s.recv(256)
            return f"CONNECTED, recv {len(data)} bytes {data[:60]!r}"
        except socket.timeout:
            return "CONNECTED, no bytes (device silent)"
    except Exception as exc:  # noqa: BLE001
        return f"{type(exc).__name__}: {exc}"
    finally:
        s.close()


def run_case(label: str, args: list[str], port: int | None, hosts: list[str],
             socket_path: Path | None = None) -> dict:
    work = Path(tempfile.mkdtemp(prefix="tio-sockcheck-"))
    out = work / "out.txt"
    err = work / "err.txt"
    with out.open("wb") as o, err.open("wb") as e:
        process = subprocess.Popen([str(EXE), *args], cwd=str(ROOT),
                                   stdout=o, stderr=e, stdin=subprocess.DEVNULL)
    try:
        time.sleep(2.5)
        alive = process.poll() is None
        listeners = tcp_listeners(port) if port else []
        connects = {host: try_connect(host, port) for host in hosts} if port else {}
        time.sleep(1.0)
        alive_after = process.poll() is None
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    result = {
        "case": label,
        "args": " ".join(args),
        "alive_after_2.5s": alive,
        "alive_after_3.5s": alive_after,
        "exit": process.returncode,
        "listeners": listeners,
        "connects": connects,
        "out": out.read_text(encoding="utf-8", errors="replace").strip()[:400],
        "err": err.read_text(encoding="utf-8", errors="replace").strip()[:400],
    }
    if socket_path is not None:
        result["socket_file_exists"] = socket_path.exists()
    print(json.dumps(result, ensure_ascii=False, indent=2))
    for path in (out, err):
        path.unlink(missing_ok=True)
    work.rmdir()
    return result


LAN = []
try:
    for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
        ip = info[4][0]
        if not ip.startswith("127.") and not ip.startswith("169.254."):
            LAN.append(ip)
except OSError:
    pass
LAN = sorted(set(LAN))
hosts = ["127.0.0.1", *LAN]

print(f"tio: {EXE}")
print(f"device: {DEVICE}")
print(f"LAN candidates: {LAN}\n")

unix_path = Path(tempfile.gettempdir()) / "tio-share-check.sock"
unix_path.unlink(missing_ok=True)

results = [
    run_case("plain serial, no socket", ["-n", DEVICE], None, []),
    run_case("inet:54321", ["-n", "-S", "inet:54321", DEVICE], 54321, hosts),
    run_case("unix:<path>", ["-n", "-S", f"unix:{unix_path}", DEVICE], None, [],
             socket_path=unix_path),
]
unix_path.unlink(missing_ok=True)

ok = [r for r in results if r["connects"] and any(v.startswith("CONNECTED") for v in r["connects"].values())]
print(f"\nsummary: {len(ok)} of {len(results)} modes accepted a client connection")
sys.exit(0 if ok else 1)
