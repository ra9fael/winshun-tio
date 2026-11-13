"""Prove the wheel does not escape the shared-content card.

The card sits inside a scrolling page. With the pointer over the terminal, the
wheel must move the terminal and nothing else; when the terminal is already at
the top or the bottom, continued wheel turns must not scroll the page behind it.

Wheel messages are posted straight at the window (WM_MOUSEWHEEL) with the screen
point in lParam: the desktop is not touched, and the window may sit on any
monitor. A control step scrolls a page area first, so a run where the injection
itself is broken fails instead of passing.
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
WM_MOUSEWHEEL = 0x020A
WHEEL_DELTA = 120


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


def capture(user32, gdi32, hwnd):
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


def wheel_at(user32, hwnd, screen_x: int, screen_y: int, turns: int) -> None:
    # lParam carries the point, in screen pixels, as two signed shorts.
    point = ((screen_y & 0xFFFF) << 16) | (screen_x & 0xFFFF)
    for _ in range(abs(turns)):
        delta = WHEEL_DELTA if turns > 0 else -WHEEL_DELTA
        user32.PostMessageW(hwnd, WM_MOUSEWHEEL, ((delta & 0xFFFF) << 16), point)
        time.sleep(0.04)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin-dir", default=str(ROOT / "build/pkg-test/unpacked"))
    parser.add_argument("--port", type=int, default=54321)
    parser.add_argument("--data-dir", default=str(ROOT / "build/pkg-test/data-wheel"))
    parser.add_argument("--shot", default=str(ROOT / "build/wheel_bottom.png"))
    args = parser.parse_args()

    from PIL import ImageChops

    plugin = Path(args.plugin_dir).resolve()
    exe = plugin / "tio.exe"
    if not exe.exists():
        print(f"FAIL no window binary at {exe}")
        return 1

    data = Path(args.data_dir).resolve()
    data.mkdir(parents=True, exist_ok=True)
    (data / "tio.json").write_text(json.dumps({
        "port": "COM10", "baudrate": 115200, "dataBits": 8, "parity": "none",
        "stopBits": 1, "flow": "none", "share": True, "sharePort": args.port,
    }, indent=2), encoding="utf-8")

    env = {**os.environ, "WS_QT_DIR": r"C:\Program Files\WinShun"}

    # A stale tio from an earlier run would hold the port; the probe would then
    # talk to it while the window's own session failed to start, and every
    # measurement below would describe an empty card.
    probe = socket.socket()
    try:
        probe.settimeout(0.5)
        if probe.connect_ex(("127.0.0.1", args.port)) == 0:
            print(f"FAIL port {args.port} is already in use; stop that process first")
            return 1
    finally:
        probe.close()

    log = open(data / "window.log", "w", encoding="utf-8")
    gui = subprocess.Popen([str(exe), "--window", "--app-dir", r"C:\Program Files\WinShun",
                            "--data-dir", str(data), "--auto", "launch"],
                           cwd=str(plugin), env=env, stdout=log, stderr=subprocess.STDOUT)

    client = None
    try:
        client = connect_with_retry(args.port, 35.0)
        if client is None:
            log.flush()
            print("FAIL no shared socket")
            print((data / "window.log").read_text(encoding="utf-8", errors="replace"))
            return 1

        # More lines than the view holds, so the terminal has somewhere to go.
        for i in range(80):
            client.sendall(f"line {i:03d} xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n".encode())
            time.sleep(0.02)
        time.sleep(2.5)

        user32 = ctypes.windll.user32
        gdi32 = ctypes.windll.gdi32
        hwnd = None
        for _ in range(40):
            hwnd = find_window(user32, gui.pid)
            if hwnd:
                break
            time.sleep(0.25)
        if not hwnd:
            print("FAIL no visible window")
            return 1

        rect = ctypes.wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        # Put the window at a fixed place: the measurements below crop fixed
        # window coordinates, and the window otherwise opens wherever the last
        # run left it (possibly on another monitor).
        user32.SetWindowPos(hwnd, 0, 120, 120, 0, 0, 0x0001 | 0x0004)  # NOSIZE|NOZORDER
        time.sleep(0.6)
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        width, height = rect.right - rect.left, rect.bottom - rect.top
        print(f"window {width}x{height} at {rect.left},{rect.top}")

        # Regions, in window-local pixels: the page header only moves when the
        # page scrolls; the terminal body only moves when the terminal scrolls.
        header = (60, 90, 60, 680)
        terminal = (60, height - 150, 140, 620)

        def crop(image, box):
            x, y, h, w = box
            return image.crop((x, y, x + w, y + h))

        def diff(a, b) -> int:
            return sum(1 for px in ImageChops.difference(a, b).getdata() if px != (0, 0, 0))

        first = capture(user32, gdi32, hwnd)
        # A flat frame means the capture failed, not that nothing moved. Only a
        # handful of distinct colours is already too few for a window.
        colours = first.getcolors(maxcolors=16)
        if colours is not None and len(colours) <= 2:
            print(f"FAIL the window captured as {len(colours)} colours")
            return 1

        page_base = crop(first, header)
        term_base = crop(first, terminal)

        # The pointer sits over the terminal for every wheel below. The page is
        # untouched at this point, so any change to the header strip is the wheel
        # escaping the card.
        pointer_x = rect.left + 300
        pointer_y = rect.top + 650

        wheel_at(user32, hwnd, pointer_x, pointer_y, 8)   # up inside the terminal
        time.sleep(0.8)
        up = capture(user32, gdi32, hwnd)
        term_moved = diff(term_base, crop(up, terminal))
        page_after_up = diff(page_base, crop(up, header))

        # Far past the top, then far past the bottom.
        wheel_at(user32, hwnd, pointer_x, pointer_y, 40)
        time.sleep(0.8)
        top = capture(user32, gdi32, hwnd)
        page_after_top = diff(page_base, crop(top, header))

        wheel_at(user32, hwnd, pointer_x, pointer_y, -90)
        time.sleep(1.0)
        bottom = capture(user32, gdi32, hwnd)
        page_after_bottom = diff(page_base, crop(bottom, header))
        crop(bottom, terminal).save(args.shot)
        bottom.save(str(Path(args.shot).with_name("wheel_full_bottom.png")))
        up.save(str(Path(args.shot).with_name("wheel_full_up.png")))

        # Control last: a wheel over a page area must move the page, so a broken
        # injection cannot make the checks above pass for nothing.
        wheel_at(user32, hwnd, rect.left + 300, rect.top + 300, -10)
        time.sleep(0.8)
        control = capture(user32, gdi32, hwnd)
        control_moved = diff(crop(bottom, header), crop(control, header))

        ok = True
        if control_moved == 0:
            print("FAIL the injected wheel did not scroll the page at all")
            ok = False
        else:
            print("PASS the injected wheel works (the page moved)")
        if term_moved == 0:
            print("FAIL the wheel did not move the terminal")
            ok = False
        else:
            print("PASS the wheel scrolls the terminal")
        for label, moved in (("while scrolling up", page_after_up),
                             ("past the top", page_after_top),
                             ("past the bottom", page_after_bottom)):
            if moved != 0:
                print(f"FAIL the page scrolled {label} ({moved} px)")
                ok = False
            else:
                print(f"PASS the page stayed put {label}")
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


if __name__ == "__main__":
    sys.exit(main())
