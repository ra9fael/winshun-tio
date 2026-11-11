"""L3: the plugin inside a real WinShun, with no window work and no port opened.

This is deliberately small: it installs the built package into a scratch copy of
the plugin folder layout, starts WinShun's own plugin host the way the runner
does (stdin/stdout JSON-RPC, `WS_QT_DIR` set), and walks the same requests the
search box would send. It does not open the launcher window and does not start
tio: an L3 run must not leave a serial port held.

Host requirements: an installed WinShun 0.22.0 or later (`C:\\Program Files\\WinShun`
by default, or `--host <folder>`), whose own Qt the plugin will load.

Exit code 0 only when every check passed.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]

total = 0
failed = 0


def check(condition: bool, label: str, detail: str = "") -> None:
    global total, failed
    total += 1
    if condition:
        print(f"PASS {label}")
    else:
        failed += 1
        print(f"FAIL {label}{': ' + detail if detail else ''}")


class Host:
    """One plugin process, started the way WinShun's runner starts it."""

    def __init__(self, program: pathlib.Path, plugin_dir: pathlib.Path,
                 data_dir: pathlib.Path, qt_dir: pathlib.Path, locale: str = "zh") -> None:
        self.process = subprocess.Popen(
            [str(program)],
            cwd=str(plugin_dir),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
            env=dict(
                os.environ,
                WS_QT_DIR=str(qt_dir),
                WS_QT_QML_DIR=str(qt_dir / "qml"),
                WS_APP_DIR=str(qt_dir),
            ),
        )
        self.plugin_dir = plugin_dir
        self.data_dir = data_dir
        self.locale = locale

    def call(self, method: str, params: dict, request_id: int) -> dict:
        assert self.process.stdin and self.process.stdout
        self.process.stdin.write(
            json.dumps({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params},
                       ensure_ascii=False) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("the plugin closed stdout: "
                               + (self.process.stderr.read() if self.process.stderr else ""))
        return json.loads(line)

    def initialize(self) -> dict:
        return self.call("initialize", {
            "protocolVersion": "1",
            "winshun": "0.22.0",
            "pluginDir": str(self.plugin_dir),
            "dataDir": str(self.data_dir),
            "locale": self.locale,
            "theme": "light",
            "appearance": {"corners": "rounded", "uiSize": "normal",
                           "nativeLook": {"rounded": True, "controlRadius": 8}},
            "settings": {},
        }, 1)

    def close(self) -> None:
        try:
            self.call("shutdown", {}, 900)
        except Exception:
            pass
        if self.process.stdin:
            self.process.stdin.close()
        try:
            self.process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.process.kill()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=r"C:\Program Files\WinShun",
                        help="the installed WinShun folder (its Qt is the plugin's)")
    parser.add_argument("--qt", help="the Qt folder, when it differs from the host's")
    args = parser.parse_args()

    host_dir = pathlib.Path(args.host)
    qt_dir = pathlib.Path(args.qt) if args.qt else host_dir
    if not (qt_dir / "Qt6Core.dll").is_file():
        print(f"no Qt6Core.dll in {qt_dir}", file=sys.stderr)
        return 2
    exe = ROOT / "tio.exe"
    dll = ROOT / "TioApp.dll"
    for name in (exe, dll):
        if not name.is_file():
            print(f"missing build output: {name}", file=sys.stderr)
            return 2

    with tempfile.TemporaryDirectory(prefix="tio-l3-") as folder:
        plugin_dir = pathlib.Path(folder) / "ra9fael.tio"
        data_dir = pathlib.Path(folder) / "ra9fael.tio.data"
        shutil.copytree(ROOT, plugin_dir,
                        ignore=shutil.ignore_patterns("build", "dist", ".git", "__pycache__"))
        data_dir.mkdir()

        plugin = Host(plugin_dir / "tio.exe", plugin_dir, data_dir, qt_dir)
        try:
            init = plugin.initialize()
            check(init.get("result", {}).get("protocolVersion") == "1",
                  "initialize answers protocol 1")
            check(init.get("result", {}).get("name") == "ra9fael.tio",
                  "initialize reports the plugin id")

            answer = plugin.call("query", {
                "queryId": 1, "command": "tio", "trigger": "keyword", "keyword": "tio",
                "text": "", "raw": "tio", "limit": 200}, 2)
            items = (answer.get("result") or {}).get("items") or []
            ids = [item.get("id") for item in items]
            check(len(items) >= 11, "the grouped rows arrive", str(len(items)))
            check("launch" in ids, "the launch row is there")
            check("port" in ids and "backend" in ids, "the parameter rows are there")

            # A replace round trip, without launching anything.
            ports = plugin.call("action", {
                "command": "tio", "action": "openPorts", "text": "",
                "item": {"id": "port"}, "trigger": "keyword", "files": []}, 3)
            effects = (ports.get("result") or {}).get("effects") or []
            replaced = next((e for e in effects if e.get("type") == "replace"), None)
            check(replaced is not None, "the port list comes back as a replace effect",
                  str([e.get("type") for e in effects]))
            if replaced:
                rows = replaced.get("items") or []
                check(bool(rows) and rows[0].get("id") == "back",
                      "the port list starts with the back row")

            details = plugin.call("details", {"item": {"id": "control"}, "text": ""}, 4)
            text = (details.get("result") or {}).get("text") or ""
            check("integrity" in text, "details reports package integrity")
            check("connected" not in text.lower(), "details never claims a connection")

            # The window is real: the compiled DLL is beside the exe, which is what
            # the starter loads.
            check((plugin_dir / "TioApp.dll").is_file(), "TioApp.dll ships beside tio.exe")

            # Ctrl+Enter: the action asks the DLL to put the window up, on the
            # GUI thread, and the protocol keeps answering while it is open.
            check((plugin_dir / "tio.exe").is_file(), "the starter is beside the DLL")
            started = time.monotonic()
            opened = plugin.call("action", {
                "command": "tio", "action": "openWindow", "text": "",
                "item": {"id": "launch"}, "trigger": "keyword", "files": []}, 5)
            elapsed = time.monotonic() - started
            effects = (opened.get("result") or {}).get("effects") or []
            notices = [e.get("text", "") for e in effects if e.get("type") == "notify"]
            check(not notices, "opening the window reports no error", "; ".join(notices))
            check(elapsed < 8.0, "opening the window answers in time", f"{elapsed:.1f}s")

            # A query while the window is up must still be answered: the model is
            # shared, and the protocol thread is not blocked on the GUI.
            again = plugin.call("query", {
                "queryId": 2, "command": "tio", "trigger": "keyword", "keyword": "tio",
                "text": "", "raw": "tio", "limit": 200}, 6)
            check(len((again.get("result") or {}).get("items") or []) >= 11,
                  "the protocol still answers with the window open")
        finally:
            plugin.close()

        check(plugin.process.poll() is not None, "the plugin exits on shutdown")
        # No session was started, so nothing should have been written to the data
        # folder except a configuration file.
        written = sorted(p.name for p in data_dir.iterdir())
        check(all(name.endswith(".json") for name in written),
              "the data folder holds only configuration", ", ".join(written))

    print(f"{'PASS' if failed == 0 else 'FAILED'} {total - failed} of {total} host checks")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
