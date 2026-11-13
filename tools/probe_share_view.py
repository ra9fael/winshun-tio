"""Photograph the window while a real shared session is running.

The window launches its own session (share on), an outside client writes ANSI
bytes to the shared socket, they echo through the serial device, and the card
draws them. This is the only check that exercises model -> QML -> pixels.

Runs against an unpacked .wsplugin, because the window refuses to launch when the
package manifest does not verify.
"""
from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

PAYLOAD = (
    "\x1b[1;32mroot@box\x1b[0m:\x1b[1;34m~\x1b[0m$ ls --color\r\n"
    "\x1b[1;34mdir1\x1b[0m  \x1b[1;32mrun.sh\x1b[0m  \x1b[38;5;208morange.txt\x1b[0m\r\n"
    "\x1b[7m STATUS \x1b[0m ready\r\n"
    "\x1b[41;97m ERROR \x1b[0m device did not answer\r\n"
)


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


def find_window(user32, pid: int):
    """The QML window of `pid`, by enumerating rather than by title."""
    found: list[int] = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)
    def visit(hwnd, _):
        owner = ctypes.wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(visit, 0)
    return found[0] if found else None


def capture(user32, gdi32, hwnd, path: str) -> None:
    rect = ctypes.wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    print(f"window {width}x{height}")

    hdc = user32.GetWindowDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bitmap = gdi32.CreateCompatibleBitmap(hdc, width, height)
    gdi32.SelectObject(mem, bitmap)
    user32.PrintWindow(hwnd, mem, 2)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", ctypes.c_uint32), ("biWidth", ctypes.c_int32),
                    ("biHeight", ctypes.c_int32), ("biPlanes", ctypes.c_uint16),
                    ("biBitCount", ctypes.c_uint16), ("biCompression", ctypes.c_uint32),
                    ("biSizeImage", ctypes.c_uint32), ("biXPelsPerMeter", ctypes.c_int32),
                    ("biYPelsPerMeter", ctypes.c_int32), ("biClrUsed", ctypes.c_uint32),
                    ("biClrImportant", ctypes.c_uint32)]

    class BITMAPINFO(ctypes.Structure):
        _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", ctypes.c_uint32 * 3)]

    info = BITMAPINFO()
    info.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    info.bmiHeader.biWidth = width
    info.bmiHeader.biHeight = -height
    info.bmiHeader.biPlanes = 1
    info.bmiHeader.biBitCount = 32
    info.bmiHeader.biCompression = 0
    buffer = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(mem, bitmap, 0, height, buffer, ctypes.byref(info), 0)

    from PIL import Image
    Image.frombuffer("RGBA", (width, height), buffer, "raw", "BGRA", 0, 1) \
         .convert("RGB").save(path)
    print("saved:", path)

    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin-dir", default=str(ROOT / "build/pkg-test/unpacked"))
    parser.add_argument("--device", default="COM10")
    parser.add_argument("--port", type=int, default=54321)
    parser.add_argument("--data-dir", default=str(ROOT / "build/pkg-test/data"))
    parser.add_argument("--shot", default=str(ROOT / "build/share_live.png"))
    parser.add_argument("--auto", default="launch")
    parser.add_argument("--settle", type=float, default=10.0)
    args = parser.parse_args()

    plugin = Path(args.plugin_dir)
    exe = plugin / "tio.exe"
    if not exe.exists():
        print(f"FAIL no window binary at {exe}")
        return 1

    data = Path(args.data_dir).resolve()
    data.mkdir(parents=True, exist_ok=True)
    # The form the window starts from: the shorted port, sharing on.
    (data / "tio.json").write_text(json.dumps({
        "port": args.device,
        "baudrate": 115200,
        "dataBits": 8,
        "parity": "none",
        "stopBits": 1,
        "flow": "none",
        "share": True,
        "sharePort": args.port,
    }, indent=2), encoding="utf-8")

    env = {**os.environ, "WS_QT_DIR": r"C:\Program Files\WinShun"}
    command = [str(exe), "--window", "--app-dir", r"C:\Program Files\WinShun",
               "--data-dir", str(data), "--auto", args.auto]
    print("run:", " ".join(command))
    # The window reports why a launch was refused on stderr; keep it so a failed
    # run explains itself instead of only saying "no socket".
    log_path = Path(args.data_dir).resolve() / "window.log"
    log = open(log_path, "w", encoding="utf-8")
    gui = subprocess.Popen(command, cwd=str(plugin), env=env,
                           stdout=log, stderr=subprocess.STDOUT)

    client = None
    try:
        # The window launches tio itself, so wait for the socket it opens.
        client = connect_with_retry(args.port, args.settle + 20.0)
        if client is None:
            log.flush()
            print("FAIL the window never opened the shared socket")
            print("--- window log ---")
            print(log_path.read_text(encoding="utf-8", errors="replace").strip())
            return 1
        print("OK   the window's session is listening")
        time.sleep(1.5)

        for line in PAYLOAD.splitlines(keepends=True):
            client.sendall(line.encode())
            time.sleep(0.3)
        time.sleep(2.5)

        user32 = ctypes.windll.user32
        gdi32 = ctypes.windll.gdi32
        hwnd = find_window(user32, gui.pid)
        for _ in range(40):
            if hwnd:
                break
            time.sleep(0.25)
            hwnd = find_window(user32, gui.pid)
        if not hwnd:
            print("FAIL no visible window for the process")
            return 1

        capture(user32, gdi32, hwnd, args.shot)
        return 0
    finally:
        if client is not None:
            client.close()
        # Kill the tree first: the session the window started is its child, and
        # terminating the parent alone leaves the child holding runtime\*.dll, so
        # the next unpack-and-run cannot replace them.
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(gui.pid)],
                       capture_output=True, text=True)
        try:
            gui.wait(timeout=10)
        except subprocess.TimeoutExpired:
            gui.kill()
        log.close()


if __name__ == "__main__":
    sys.exit(main())
