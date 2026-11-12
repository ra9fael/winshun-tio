"""Protocol round trip against the built plugin, without the WinShun host.

L3 proper (the real host) is host_smoke.py; this script covers the JSON-RPC
shape the host relies on, so a broken answer is caught before installing.

Path A (the search box) is what runs here: query builds the grouped rows,
action edits in place and answers with effect::replace carrying the candidate
list. By default nothing is launched on a port: every check stops at
query/action/details.

`--launch` adds the one case that needs a real process: Enter on the launch row,
the running row, and Enter again to stop. That holds the serial port for as long
as the terminal lives, so it is opt-in.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXE = ROOT / "tio.exe"

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


class Plugin:
    """One plugin process, one request at a time."""

    def __init__(self, data_dir: pathlib.Path) -> None:
        self.data_dir = data_dir
        # WS_QT_DIR is what the host injects for a "qt": "host" plugin: without
        # it the window honestly says it needs the Qt build, which is its own
        # contract, not a failure.
        env = dict(os.environ)
        if not (env.get("WS_QT_DIR") or (pathlib.Path(r"C:\Program Files\WinShun") / "Qt6Core.dll").is_file()):
            env["WS_QT_DIR_ABSENT"] = "1"
        else:
            env.setdefault("WS_QT_DIR", r"C:\Program Files\WinShun")
        self.process = subprocess.Popen(
            [str(EXE)],
            cwd=str(ROOT),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
            env=env,
        )

    def send(self, payload: dict) -> None:
        assert self.process.stdin
        self.process.stdin.write(json.dumps(payload, ensure_ascii=False) + "\n")
        self.process.stdin.flush()

    def read(self) -> dict:
        assert self.process.stdout
        line = self.process.stdout.readline()
        if not line:
            stderr = self.process.stderr.read() if self.process.stderr else ""
            raise RuntimeError(f"the plugin closed stdout; stderr={stderr!r}")
        return json.loads(line)

    def call(self, method: str, params: dict, request_id: int | None = None) -> dict:
        request: dict = {"jsonrpc": "2.0", "method": method, "params": params}
        if request_id is not None:
            request["id"] = request_id
        self.send(request)
        return self.read()

    def initialize(self, locale: str = "zh", theme: str = "light",
                   settings: dict | None = None) -> dict:
        return self.call(
            "initialize",
            {
                "protocolVersion": "1",
                "winshun": "0.22.0",
                "pluginDir": str(ROOT),
                "dataDir": str(self.data_dir),
                "locale": locale,
                "theme": theme,
                "appearance": {
                    "corners": "rounded",
                    "uiSize": "normal",
                    "nativeLook": {"rounded": True, "controlRadius": 8, "dark": theme == "dark"},
                },
                "settings": settings or {},
            },
            1,
        )

    def query(self, request_id: int = 2) -> list[dict]:
        answer = self.call(
            "query",
            {"queryId": request_id, "command": "tio", "trigger": "keyword", "keyword": "tio",
             "text": "", "raw": "tio", "limit": 200},
            request_id,
        )
        return answer_items(answer)

    def action(self, action_id: str, item: dict | None = None, request_id: int = 10) -> list[dict]:
        answer = self.call(
            "action",
            {"command": "tio", "action": action_id, "text": "",
             "item": item or {}, "trigger": "keyword", "files": []},
            request_id,
        )
        return (answer.get("result") or {}).get("effects") or []

    def close(self) -> None:
        try:
            self.call("shutdown", {}, 99)
        except Exception:
            pass
        if self.process.stdin:
            self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()


def answer_items(answer: dict) -> list[dict]:
    result = answer.get("result") or {}
    return result.get("items") or []


def replaced(effects: list[dict]) -> list[dict]:
    """The rows a `replace` effect carries, or [] when there is none."""
    for effect in effects:
        if effect.get("type") == "replace":
            return effect.get("items") or []
    return []


def effect_types(effects: list[dict]) -> list[str]:
    return [str(e.get("type")) for e in effects]


def find(items: list[dict], item_id: str) -> dict:
    for item in items:
        if item.get("id") == item_id:
            return item
    return {}


def launch_row(items: list[dict]) -> dict:
    """The first per-port launch row: the main list has one row per port."""
    for item in items:
        if not str(item.get("id", "")).startswith("port:"):
            continue
        actions = item.get("actions") or [{}]
        if actions[0].get("id") in ("launchPort", "stopPort"):
            return item
    return {}


def launch_rows(items: list[dict]) -> list[dict]:
    return [item for item in items
            if str(item.get("id", "")).startswith("port:")
            and (item.get("actions") or [{}])[0].get("id") in ("launchPort", "stopPort")]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--launch", action="store_true",
                        help="also launch and stop a real session (holds the serial port)")
    args = parser.parse_args()

    if not EXE.is_file():
        print(f"the plugin exe is missing: {EXE}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="tio-data-") as folder:
        data_dir = pathlib.Path(folder)
        plugin = Plugin(data_dir)
        try:
            init = plugin.initialize()
            check(init.get("result", {}).get("protocolVersion") == "1", "initialize answers protocol 1")
            check(init.get("result", {}).get("name") == "ra9fael.tio", "initialize reports the plugin id")

            # ---- path A: the grouped answer ----
            items = plugin.query()
            ids = [i.get("id") for i in items]
            check(len(items) >= 11, "the answer carries the grouped rows", str(len(items)))
            check(len(launch_rows(items)) >= 1, "every enumerated port has a launch row",
                  str(len(launch_rows(items))))
            check(len({i.get("id") for i in launch_rows(items)}) == len(launch_rows(items)),
                  "the launch rows have distinct ids")
            for wanted in ("port", "baud", "dataBits", "parity", "stopBits", "flow", "backend",
                           "control", "diagnostics"):
                check(wanted in ids, f"the {wanted} row is present")

            launch = launch_row(items)
            check(launch.get("title", "").startswith("tio"), "the launch row names tio",
                  launch.get("title", ""))
            # A row that cannot start says why instead of going quietly dead.
            if launch.get("valid") is False:
                check("阻塞" in (launch.get("subtitle") or "")
                      or "blocked" in (launch.get("subtitle") or ""),
                      "a disabled launch row names the failing gate",
                      launch.get("subtitle", ""))
            actions = launch.get("actions") or []
            check(bool(actions), "the launch row carries an action")
            check(actions[0].get("key") == "Enter", "the launch row is on Enter")

            port = find(items, "port")
            check((port.get("actions") or [{}])[0].get("id") == "openPorts",
                  "the port row opens the port list")
            parity = find(items, "parity")
            check((parity.get("actions") or [{}])[0].get("id") == "cycleParity",
                  "the parity row cycles in place")
            check(bool(find(items, "control").get("valid")) is False,
                  "the control-line row is read-only")
            diag = find(items, "diagnostics")
            check((diag.get("actions") or [{}])[0].get("id") == "diagnostics",
                  "the diagnostics row opens the diagnostics view")
            check(bool(diag.get("copy")), "the diagnostics row carries the report for Shift+Enter")

            # ---- opening a candidate list, then going back ----
            ports = replaced(plugin.action("openPorts", find(items, "port"), 11))
            check(bool(ports), "openPorts answers with a replace effect")
            check(ports and ports[0].get("id") == "back", "the port list starts with the back row")
            listed = [i for i in ports if str(i.get("id", "")).startswith("port:")]
            for row in listed:
                title = row.get("title") or ""
                check(title.startswith("COM"), "a port row titles its COMn", title)
                check("/dev/ttyS" in (row.get("subtitle") or ""),
                      "a port row shows the ttyS mapping", row.get("subtitle") or "")

            back = replaced(plugin.action("back", {"id": "back"}, 12))
            check(bool(launch_row(back)), "back returns the main list")

            bauds = replaced(plugin.action("openBaudrates", find(items, "baud"), 13))
            check(len([i for i in bauds if str(i.get("id", "")).startswith("baud:")]) >= 5,
                  "the baud list offers the supported rates")

            backends = replaced(plugin.action("openBackends", find(items, "backend"), 14))
            wt = find(backends, "backend:wt")
            con = find(backends, "backend:conhost")
            check(bool(wt) and bool(con), "the backend list offers wt and conhost")
            check(wt.get("subtitle") is not None, "the wt row reports its version")

            # ---- choosing from a list writes the configuration ----
            before = json.loads((data_dir / "tio.json").read_text(encoding="utf-8")) \
                if (data_dir / "tio.json").is_file() else {}
            if wt.get("valid") is not False:
                chosen = replaced(plugin.action("backend", wt, 15))
                check(bool(launch_row(chosen)), "choosing a backend returns the main list")
                saved = json.loads((data_dir / "tio.json").read_text(encoding="utf-8"))
                check(saved.get("backend") == "wt", "the backend choice is written", str(saved.get("backend")))
            else:
                check(True, "wt is absent on this machine, so the backend choice is skipped")

            # ---- cycling a short list ----
            current = find(plugin.query(20), "parity").get("subtitle")
            cycled = replaced(plugin.action("cycleParity", find(items, "parity"), 16))
            after = find(cycled, "parity").get("subtitle")
            check(after != current, "cycleParity changes the value", f"{current} -> {after}")
            saved = json.loads((data_dir / "tio.json").read_text(encoding="utf-8"))
            check(saved.get("parity") == after, "the parity cycle is written", str(saved.get("parity")))
            # Four more cycles return to where it started.
            for step in range(4):
                cycled = replaced(plugin.action("cycleParity", find(cycled, "parity"), 17 + step))
            check(find(cycled, "parity").get("subtitle") == current, "five parity cycles come back",
                  find(cycled, "parity").get("subtitle", ""))

            # ---- diagnostics: expand in the list, copy on Shift+Enter ----
            view = replaced(plugin.action("diagnostics", find(items, "diagnostics"), 30))
            check(bool(view), "diagnostics answers with a replace effect")
            check(view and view[0].get("id") == "back", "the diagnostics view starts with back")
            lines = [i for i in view if str(i.get("id", "")).startswith("diag:")]
            check(len(lines) >= 5, "the report is one row per line", str(len(lines)))
            check(all(i.get("valid") is False for i in lines),
                  "every diagnostics row is read-only")
            check("ra9fael.tio" in (lines[0].get("title") or ""),
                  "the first row is the report header", lines[0].get("title", ""))
            check(lines[0].get("copy") == find(items, "diagnostics").get("copy"),
                  "the first row carries the whole report for copying")
            check((lines[0].get("actions") or [{}])[0].get("id") == "copyDiagnostics",
                  "the first row offers copy on Shift+Enter")
            check(all(not i.get("copy") for i in lines[1:]),
                  "no other row carries a copy")
            check("connected" not in json.dumps(view).lower(),
                  "the diagnostics view never claims a connection")

            # Shift+Enter on the view copies the report and keeps the view.
            copied = plugin.action("copyDiagnostics", lines[0], 32)
            check("copy" in effect_types(copied), "copyDiagnostics copies the report",
                  str(effect_types(copied)))
            report = next((e.get("text") for e in copied if e.get("type") == "copy"), "")
            check("integrity" in report, "the copied report has integrity")
            check("no serial payload" in report, "the copied report states the privacy boundary")

            # Back returns to the main list.
            back = replaced(plugin.action("back", {"id": "back"}, 33))
            check(bool(launch_row(back)), "back returns the main list")

            details = plugin.call("details", {"item": find(items, "control"), "text": ""}, 34)
            text = (details.get("result") or {}).get("text") or ""
            check("backend" in text, "details reports the backend")
            check("session" in text, "details reports the session")
            check("connected" not in text.lower(), "details never claims a connection")

            # ---- language ----
            zh_groups = [i["id"] for i in items if str(i.get("id", "")).startswith("group:")]
            plugin.send({"jsonrpc": "2.0", "method": "settingsChanged", "params": {"settings": {}}})
            plugin.send({"jsonrpc": "2.0", "method": "appearance",
                         "params": {"locale": "en", "theme": "dark", "corners": "square"}})
            english = plugin.query(40)
            check(launch_row(english).get("title", "").startswith("tio"),
                  "the launch row survives a language change")
            check(find(english, "port").get("title") == "Port", "the port row is localized",
                  find(english, "port").get("title", ""))
            check(find(english, "control").get("valid") is False, "the control row stays read-only")
            # A group row's id must not change with the language: the host keeps
            # the keyboard on the row with the same id when the list is re-answered.
            en_groups = [i["id"] for i in english if str(i.get("id", "")).startswith("group:")]
            check(zh_groups == en_groups, "the group row ids are language-independent",
                  f"{zh_groups} vs {en_groups}")

            # ---- the real thing: launch, the running row, stop ----
            if args.launch:
                ready = launch_row(plugin.query(70))
                if ready.get("valid") is False:
                    # No port, no backend or a broken package: the row says which,
                    # and launching is the one thing that must not be faked.
                    print(f"SKIP no launch on this machine: {ready.get('subtitle', '')}")
                else:
                    started = plugin.action("launchPort", ready, 71)
                    replaced_rows = replaced(started)
                    running = launch_row(replaced_rows)
                    check((running.get("actions") or [{}])[0].get("id") == "stopPort",
                          "a started session offers Stop on the same row",
                          str(running.get("actions")))
                    check("Running" in (running.get("subtitle") or "")
                          or "运行中" in (running.get("subtitle") or ""),
                          "the running row says it is running", running.get("subtitle", ""))
                    # A started process is not a connected port.
                    check("connected" not in (running.get("subtitle") or "").lower(),
                          "the running row does not claim a connection")

                    saved = json.loads((data_dir / "tio.json").read_text(encoding="utf-8"))
                    check(saved.get("port", "").startswith("COM"),
                          "the launched configuration is on disk", str(saved.get("port")))
                    check(saved.get("packageVersion") == "3.9-3",
                          "the configuration records the package version",
                          str(saved.get("packageVersion")))

                    time.sleep(1)
                    stopped = plugin.action("stopPort", running, 72)
                    after = launch_row(replaced(stopped))
                    check((after.get("actions") or [{}])[0].get("id") == "launchPort",
                          "after Stop the row offers Launch again",
                          str(after.get("actions")))

            # ---- the standalone window's entries ----
            # Its own row (visible), and Ctrl+Enter on the launch row (the menu).
            entry = find(items, "openWindow")
            check(bool(entry), "the window entry row is in the list")
            check((entry.get("actions") or [{}])[0].get("id") == "openWindow",
                  "the entry row opens the window")
            check(entry.get("valid") is not False, "the entry row is selectable")
            launch_actions = [a.get("id") for a in (launch.get("actions") or [])]
            check("openWindow" in launch_actions,
                  "the launch row's menu opens the window too", str(launch_actions))

            # Ctrl+Enter / the entry row: the window comes up on the GUI thread,
            # and the protocol keeps answering while it is open.
            opened = plugin.action("openWindow", entry, 35)
            notices = [e.get("text", "") for e in opened if e.get("type") == "notify"]
            check(not notices, "opening the window reports no error", "; ".join(notices))
            again = plugin.query(36)
            check(len(again) >= 11, "the protocol still answers with the window open")

            # ---- refusals ----
            other = plugin.call(
                "query",
                {"queryId": 50, "command": "something-else", "trigger": "keyword", "keyword": "x",
                 "text": "", "raw": "x", "limit": 20},
                50,
            )
            check(answer_items(other) == [], "another command's query returns nothing")
            unknown = plugin.call("notAMethod", {}, 51)
            check("error" in unknown, "an unknown method is an error")
            unsupported = plugin.action("noSuchAction", {"id": "launch"}, 52)
            check("notify" in effect_types(unsupported), "an unknown action answers with a notice",
                  str(effect_types(unsupported)))
        finally:
            plugin.close()

        check(plugin.process.poll() is not None, "the plugin exits on shutdown")

        # The settings file is the only thing that persists: a second run must
        # start from it without a window or a session.
        plugin = Plugin(data_dir)
        try:
            init = plugin.initialize()
            check("result" in init, "the plugin starts with an existing data folder")
            restored = launch_row(plugin.query(60))
            check(str(restored.get("id", "")).startswith("port:"),
                  "the second run answers the same rows", str(restored.get("id")))
        finally:
            plugin.close()

        # The host's settings page seeds a value only where tio.json has none, and
        # a value already recorded there wins: the user's own edit is newer.
        fresh = data_dir / "settings-seed"
        fresh.mkdir()
        plugin = Plugin(fresh)
        try:
            # A settings page sends a text field as a string ("8", not 8), which is
            # the case the seeding must handle too.
            plugin.initialize(settings={"baudrate": 9600, "dataBits": "7", "parity": "even",
                                        "backend": "conhost"})
            seeded = launch_row(plugin.query(61))
            check("9600" in (seeded.get("subtitle") or ""),
                  "the host's settings seed a fresh configuration", seeded.get("subtitle", ""))
            check("7e1" in (seeded.get("subtitle") or ""),
                  "a numeric setting sent as text is read", seeded.get("subtitle", ""))
            check("conhost" in str(find(plugin.query(62), "backend")),
                  "the host's backend choice is used",
                  find(plugin.query(63), "backend").get("subtitle", ""))
            # An edit through the box is written, and then wins over the settings.
            plugin.action("cycleParity", find(plugin.query(64), "parity"), 65)
            plugin.send({"jsonrpc": "2.0", "method": "settingsChanged",
                         "params": {"settings": {"baudrate": 300, "parity": "odd"}}})
            plugin.send({"jsonrpc": "2.0", "method": "appearance",
                         "params": {"locale": "zh", "theme": "light"}})
            after = launch_row(plugin.query(66))
            check("300" not in (after.get("subtitle") or ""),
                  "a settings change does not reset the saved baud rate",
                  after.get("subtitle", ""))
        finally:
            plugin.close()

    print(f"{'PASS' if failed == 0 else 'FAILED'} {total - failed} of {total} protocol checks")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
