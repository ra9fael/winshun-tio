"""Watch what the clipboard actually holds after the window's Copy button.

A clipboard manager can take the clipboard, read it, and rewrite it — the
sequence number moves either way, so "the number changed" alone cannot tell
"our text is on the clipboard" from "someone replaced it". This samples the text
and the format list over a second, which shows which of the two happened.
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
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
MK_LBUTTON = 0x0001
CF_UNICODETEXT = 13

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

# 64-bit handles: without these declarations ctypes truncates them to int.
user32.GetClipboardData.restype = ctypes.c_void_p
user32.GetClipboardData.argtypes = [ctypes.c_uint]
user32.SetClipboardData.restype = ctypes.c_void_p
user32.SetClipboardData.argtypes = [ctypes.c_uint, ctypes.c_void_p]
kernel32.GlobalLock.restype = ctypes.c_void_p
kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]
kernel32.GlobalAlloc.restype = ctypes.c_void_p
kernel32.GlobalAlloc.argtypes = [ctypes.c_uint, ctypes.c_size_t]


def connect_with_retry(port: int, deadline_s: float):
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


def find_window(pid: int):
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


def read_text() -> tuple[str, bool]:
    """The clipboard text, and whether the clipboard could be opened at all."""
    if not user32.OpenClipboard(None):
        return "", False
    try:
        handle = user32.GetClipboardData(CF_UNICODETEXT)
        if not handle:
            return "", True
        pointer = kernel32.GlobalLock(handle)
        if not pointer:
            return "", True
        try:
            return ctypes.wstring_at(pointer), True
        finally:
            kernel32.GlobalUnlock(handle)
    finally:
        user32.CloseClipboard()


def formats() -> list[str]:
    names = {13: "CF_UNICODETEXT", 1: "CF_TEXT", 16: "CF_LOCALE", 7: "CF_OEMTEXT"}
    out: list[str] = []
    if not user32.OpenClipboard(None):
        return ["<clipboard busy>"]
    try:
        fmt = 0
        while True:
            fmt = user32.EnumClipboardFormats(fmt)
            if fmt == 0:
                break
            out.append(names.get(fmt, f"0x{fmt:x}"))
    finally:
        user32.CloseClipboard()
    return out


def set_sentinel(text: str) -> None:
    data = ctypes.create_unicode_buffer(text)
    size = ctypes.sizeof(data)
    handle = kernel32.GlobalAlloc(0x0002, size)
    pointer = kernel32.GlobalLock(handle)
    ctypes.memmove(pointer, data, size)
    kernel32.GlobalUnlock(handle)
    for _ in range(20):
        if user32.OpenClipboard(None):
            user32.EmptyClipboard()
            user32.SetClipboardData(CF_UNICODETEXT, handle)
            user32.CloseClipboard()
            return
        time.sleep(0.05)


def click(hwnd, screen_x: int, screen_y: int) -> None:
    point = ctypes.wintypes.POINT(screen_x, screen_y)
    user32.ScreenToClient(hwnd, ctypes.byref(point))
    packed = ((point.y & 0xFFFF) << 16) | (point.x & 0xFFFF)
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, packed)
    time.sleep(0.08)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, packed)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin-dir", default=str(ROOT / "build/pkg-test/unpacked"))
    parser.add_argument("--port", type=int, default=54321)
    parser.add_argument("--data-dir", default=str(ROOT / "build/pkg-test/data-watch"))
    parser.add_argument("--x", type=int, default=533)
    parser.add_argument("--y", type=int, default=524)
    args = parser.parse_args()

    plugin = Path(args.plugin_dir).resolve()
    exe = plugin / "tio.exe"
    data = Path(args.data_dir).resolve()
    data.mkdir(parents=True, exist_ok=True)
    (data / "tio.json").write_text(json.dumps({
        "port": "COM10", "baudrate": 115200, "dataBits": 8, "parity": "none",
        "stopBits": 1, "flow": "none", "share": True, "sharePort": args.port,
    }, indent=2), encoding="utf-8")

    probe = socket.socket()
    probe.settimeout(0.5)
    if probe.connect_ex(("127.0.0.1", args.port)) == 0:
        print(f"FAIL port {args.port} is already in use")
        return 1
    probe.close()

    set_sentinel("SENTINEL")
    print("before:", read_text()[0][:40], "| seq", user32.GetClipboardSequenceNumber())

    env = {**os.environ, "WS_QT_DIR": r"C:\Program Files\WinShun"}
    log = open(data / "window.log", "w", encoding="utf-8")
    gui = subprocess.Popen([str(exe), "--window", "--app-dir", r"C:\Program Files\WinShun",
                            "--data-dir", str(data), "--auto", "launch"],
                           cwd=str(plugin), env=env, stdout=log, stderr=subprocess.STDOUT)

    client = None
    try:
        client = connect_with_retry(args.port, 35.0)
        if client is None:
            print("FAIL no shared socket")
            return 1
        for line in ("root@box:~$ ls --color\r\n", "dir1  run.sh\r\n"):
            client.sendall(line.encode())
            time.sleep(0.25)
        time.sleep(2.5)

        hwnd = None
        for _ in range(40):
            hwnd = find_window(gui.pid)
            if hwnd:
                break
            time.sleep(0.25)
        if not hwnd:
            print("FAIL no visible window")
            return 1

        user32.SetWindowPos(hwnd, 0, 120, 120, 0, 0, 0x0001 | 0x0004)
        time.sleep(0.8)
        rect = ctypes.wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))

        click(hwnd, rect.left + args.x, rect.top + args.y)

        # Sample right after the click: a manager that rewrites the clipboard
        # shows up as the text changing back to something else.
        for delay in (0.0, 0.05, 0.15, 0.4, 0.9, 1.8):
            time.sleep(delay if delay else 0.05)
            text, opened = read_text()
            print(f"t+{delay:>4}s seq={user32.GetClipboardSequenceNumber():>4} "
                  f"opened={opened} text={text[:44]!r}")
        print("formats:", formats())
        return 0
    finally:
        if client is not None:
            client.close()
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(gui.pid)],
                       capture_output=True, text=True)
        try:
            gui.wait(timeout=10)
        except subprocess.TimeoutExpired:
            gui.kill()
        log.close()


if __name__ == "__main__":
    sys.exit(main())
