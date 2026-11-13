"""Reproduce WinShun's clipboard sandbox and check what the Copy button does.

WinShun runs plugins inside a job object and enforces the manifest's clipboard
permission with JOB_OBJECT_UILIMIT_WRITECLIPBOARD. Under that restriction
OpenClipboard and EmptyClipboard still succeed — which raises the clipboard
sequence number — while SetClipboardData is refused. A copy that judges success
by the sequence number therefore reports "copied" and writes nothing.

This runs the window inside such a job and checks two things the user feels:
the clipboard is not left emptied, and the message names the permission.

--restrict off runs the same steps without the restriction, as the control.
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
JOB_OBJECT_UILIMIT_WRITECLIPBOARD = 0x00000004
JOB_OBJECT_BASIC_UI_RESTRICTIONS = 4

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

user32.GetClipboardData.restype = ctypes.c_void_p
user32.GetClipboardData.argtypes = [ctypes.c_uint]
user32.SetClipboardData.restype = ctypes.c_void_p
user32.SetClipboardData.argtypes = [ctypes.c_uint, ctypes.c_void_p]
kernel32.GlobalLock.restype = ctypes.c_void_p
kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]
kernel32.GlobalAlloc.restype = ctypes.c_void_p
kernel32.GlobalAlloc.argtypes = [ctypes.c_uint, ctypes.c_size_t]


class UI_RESTRICTIONS(ctypes.Structure):
    _fields_ = [("UIRestrictionsClass", ctypes.c_uint32)]


def make_job(restrict: bool):
    """A job like WinShun's runner makes, optionally denying clipboard writes."""
    kernel32.CreateJobObjectW.restype = ctypes.c_void_p
    job = kernel32.CreateJobObjectW(None, None)
    if not job:
        raise OSError("CreateJobObject failed")
    if restrict:
        ui = UI_RESTRICTIONS(JOB_OBJECT_UILIMIT_WRITECLIPBOARD)
        if not kernel32.SetInformationJobObject(job, JOB_OBJECT_BASIC_UI_RESTRICTIONS,
                                                ctypes.byref(ui), ctypes.sizeof(ui)):
            raise OSError("SetInformationJobObject failed")
    return job


def assign(job, process: subprocess.Popen) -> bool:
    kernel32.AssignProcessToJobObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    return bool(kernel32.AssignProcessToJobObject(job, int(process._handle)))


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


def read_text() -> str:
    if not user32.OpenClipboard(None):
        return "<clipboard busy>"
    try:
        handle = user32.GetClipboardData(CF_UNICODETEXT)
        if not handle:
            return "<empty>"
        pointer = kernel32.GlobalLock(handle)
        if not pointer:
            return "<locked>"
        try:
            return ctypes.wstring_at(pointer)
        finally:
            kernel32.GlobalUnlock(handle)
    finally:
        user32.CloseClipboard()


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
    parser.add_argument("--data-dir", default=str(ROOT / "build/pkg-test/data-perm"))
    parser.add_argument("--x", type=int, default=533)
    parser.add_argument("--y", type=int, default=524)
    parser.add_argument("--restrict", choices=["on", "off"], default="on")
    args = parser.parse_args()

    plugin = Path(args.plugin_dir).resolve()
    exe = plugin / "tio.exe"
    if not exe.exists():
        print(f"FAIL no window binary at {exe}")
        return 1

    probe = socket.socket()
    probe.settimeout(0.5)
    if probe.connect_ex(("127.0.0.1", args.port)) == 0:
        probe.close()
        print(f"FAIL port {args.port} is already in use")
        return 1
    probe.close()

    data = Path(args.data_dir).resolve()
    data.mkdir(parents=True, exist_ok=True)
    (data / "tio.json").write_text(json.dumps({
        "port": "COM10", "baudrate": 115200, "dataBits": 8, "parity": "none",
        "stopBits": 1, "flow": "none", "share": True, "sharePort": args.port,
    }, indent=2), encoding="utf-8")

    set_sentinel("SENTINEL")
    restricted = args.restrict == "on"
    job = make_job(restricted)
    print(f"job created, clipboard writes {'DENIED' if restricted else 'allowed'}")

    env = {**os.environ, "WS_QT_DIR": r"C:\Program Files\WinShun"}
    log_path = data / "window.log"
    log = open(log_path, "w", encoding="utf-8")
    gui = subprocess.Popen([str(exe), "--window", "--app-dir", r"C:\Program Files\WinShun",
                            "--data-dir", str(data), "--auto", "launch"],
                           cwd=str(plugin), env=env, stdout=log, stderr=subprocess.STDOUT)
    if not assign(job, gui):
        print("FAIL could not put the window in the job")
        gui.kill()
        return 1

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
        time.sleep(1.2)

        text = read_text()
        log.flush()
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
        denied_message = "剪贴板写入权限" in log_text or "clipboard write access" in log_text
        copied_message = "已复制" in log_text or "copied " in log_text

        print(f"clipboard: {text[:50]!r}")
        print(f"log says copied : {copied_message}")
        print(f"log says denied : {denied_message}")

        if restricted:
            ok = (text == "SENTINEL") and denied_message and not copied_message
            print("PASS" if ok else "FAIL",
                  "a denied clipboard write is reported and the old content is kept")
            return 0 if ok else 1

        ok = ("root@box" in text) and copied_message
        print("PASS" if ok else "FAIL", "an allowed clipboard write lands the text")
        return 0 if ok else 1
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
        kernel32.CloseHandle(ctypes.c_void_p(job))


if __name__ == "__main__":
    sys.exit(main())
