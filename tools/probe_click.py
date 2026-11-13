"""Click the window's buttons with real mouse messages and check what happened.

The auto-action path calls the button's handler directly, so it proves the model
but not the hit test. A button inside a folding card can be shadowed by the
card's own click area, and then a real click looks like "the button does
nothing". This posts WM_LBUTTONDOWN/UP at client coordinates and compares the
clipboard, so the click goes through the same path a person's does.
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
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
MK_LBUTTON = 0x0001


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


def find_window(user32, pid: int):
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


def click(user32, hwnd, screen_x: int, screen_y: int) -> None:
    """A real left click at a screen point.

    WM_LBUTTONDOWN carries *client* coordinates, so the point is converted here:
    passing image coordinates straight in lands the click wherever the title bar
    and border happen to shift it, which is how a button appears to do nothing.
    """
    point = ctypes.wintypes.POINT(screen_x, screen_y)
    user32.ScreenToClient(hwnd, ctypes.byref(point))
    packed = ((point.y & 0xFFFF) << 16) | (point.x & 0xFFFF)
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, packed)
    time.sleep(0.08)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, packed)
    time.sleep(0.4)


def grab(user32, gdi32, hwnd):
    from PIL import Image
    rect = ctypes.wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
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
    image = Image.frombuffer("RGBA", (width, height), buffer, "raw", "BGRA", 0, 1).convert("RGB")
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    return image


class ClipboardHog(threading.Thread):
    """Stand-in for WinShun's clipboard history.

    The host opens the clipboard every time its content changes, which is enough
    to refuse a copy that arrives in the same instant. This hammers the same way,
    so a copy that only *looks* successful — or one wrongly reported as blocked —
    shows up here.
    """

    def __init__(self, user32):
        super().__init__(daemon=True)
        self.user32 = user32
        self.stop = threading.Event()

    def run(self) -> None:
        while not self.stop.is_set():
            if self.user32.OpenClipboard(None):
                time.sleep(0.01)
                self.user32.CloseClipboard()
            time.sleep(0.005)


def clipboard_text(user32, kernel32) -> str:
    """Read the clipboard through the Win32 API, not through a shell round trip."""
    CF_UNICODETEXT = 13
    user32.GetClipboardData.restype = ctypes.c_void_p
    user32.GetClipboardData.argtypes = [ctypes.c_uint]
    kernel32.GlobalLock.restype = ctypes.c_void_p
    kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
    kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]
    if not user32.OpenClipboard(None):
        return ""
    try:
        handle = user32.GetClipboardData(CF_UNICODETEXT)
        if not handle:
            return ""
        pointer = kernel32.GlobalLock(handle)
        if not pointer:
            return ""
        try:
            return ctypes.wstring_at(pointer)
        finally:
            kernel32.GlobalUnlock(handle)
    finally:
        user32.CloseClipboard()


def set_clipboard_text(user32, kernel32, text: str) -> None:
    CF_UNICODETEXT = 13
    GMEM_MOVEABLE = 0x0002
    kernel32.GlobalAlloc.restype = ctypes.c_void_p
    kernel32.GlobalAlloc.argtypes = [ctypes.c_uint, ctypes.c_size_t]
    kernel32.GlobalLock.restype = ctypes.c_void_p
    kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
    kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]
    user32.SetClipboardData.restype = ctypes.c_void_p
    user32.SetClipboardData.argtypes = [ctypes.c_uint, ctypes.c_void_p]

    data = ctypes.create_unicode_buffer(text)
    size = ctypes.sizeof(data)
    handle = kernel32.GlobalAlloc(GMEM_MOVEABLE, size)
    pointer = kernel32.GlobalLock(handle)
    ctypes.memmove(pointer, data, size)
    kernel32.GlobalUnlock(handle)
    user32.OpenClipboard(None)
    user32.EmptyClipboard()
    user32.SetClipboardData(CF_UNICODETEXT, handle)
    user32.CloseClipboard()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin-dir", default=str(ROOT / "build/pkg-test/unpacked"))
    parser.add_argument("--port", type=int, default=54321)
    parser.add_argument("--data-dir", default=str(ROOT / "build/pkg-test/data-click"))
    parser.add_argument("--x", type=int, default=533)
    parser.add_argument("--y", type=int, default=524)
    parser.add_argument("--auto", default="launch")
    parser.add_argument("--hog", action="store_true",
                        help="hold the clipboard in a loop, like a clipboard history does")
    parser.add_argument("--expect", default="root@box",
                        help="substring the clipboard must contain after the click")
    parser.add_argument("--shot", default=str(ROOT / "build/click_after.png"))
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

    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    gdi32 = ctypes.windll.gdi32
    set_clipboard_text(user32, kernel32, "CLIPBOARD-SENTINEL")

    env = {**os.environ, "WS_QT_DIR": r"C:\Program Files\WinShun"}
    log = open(data / "window.log", "w", encoding="utf-8")
    gui = subprocess.Popen([str(exe), "--window", "--app-dir", r"C:\Program Files\WinShun",
                            "--data-dir", str(data), "--auto", args.auto],
                           cwd=str(plugin), env=env, stdout=log, stderr=subprocess.STDOUT)

    client = None
    hog = None
    try:
        client = connect_with_retry(args.port, 35.0)
        if client is None:
            print("FAIL no shared socket")
            return 1
        payload = ("root@box:~$ ls --color\r\n"
                   "dir1  run.sh  orange.txt\r\n")
        for line in payload.splitlines(keepends=True):
            client.sendall(line.encode())
            time.sleep(0.25)
        time.sleep(2.5)

        hwnd = None
        for _ in range(40):
            hwnd = find_window(user32, gui.pid)
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

        before = grab(user32, gdi32, hwnd)

        if args.hog:
            hog = ClipboardHog(user32)
            hog.start()
            print("clipboard hog started (stands in for WinShun's history)")
            time.sleep(0.5)

        # --x/--y are image offsets inside the window; the click is posted in
        # screen space and converted, so a border never shifts it.
        click(user32, hwnd, rect.left + args.x, rect.top + args.y)
        time.sleep(0.8)

        after = grab(user32, gdi32, hwnd)
        after.save(args.shot)

        # Read until the clipboard can be opened: with a history manager around,
        # a single read can be refused and return nothing, which would look like
        # an empty copy rather than a busy clipboard.
        text = ""
        for _ in range(40):
            text = clipboard_text(user32, kernel32)
            if text:
                break
            time.sleep(0.05)

        print(f"clipboard after click: {text[:60]!r}")
        if text == "CLIPBOARD-SENTINEL" or not text:
            print("FAIL the click did not put anything on the clipboard")
            from PIL import ImageChops
            diff = sum(1 for px in ImageChops.difference(before, after).getdata()
                       if px != (0, 0, 0))
            print(f"      window pixels changed by the click: {diff}")
            return 1
        if args.expect and args.expect not in text:
            print(f"FAIL the clipboard does not contain {args.expect!r}")
            return 1
        print("PASS a real mouse click copies the shared content")
        return 0
    finally:
        if hog is not None:
            hog.stop.set()
            hog.join(timeout=2)
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
