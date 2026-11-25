"""Protocol round trip against the built plugin, without the WinShun host.

L3 proper (the real host) is host_smoke.py; this script covers the JSON-RPC
shape the host relies on, so a broken answer is caught before installing.

Path A (the search box) is what runs here: query builds the grouped rows,
action edits in place and answers with effect::replace carrying the candidate
list. The three keys the host's settings page owns (port, baud rate, backend)
are announced to the host over winshun/settings/update and stay out of
tio.json; the advanced keys persist in the file. By default nothing is
launched on a port: every check stops at query/action/details.

`--launch` adds the cases that need a real process: Enter on the launch row, the
running row, and Enter again to stop; and, when the machine has a second device,
a session started on it while the first one runs. That holds the serial ports for
as long as the terminals live, so it is opt-in.
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
        self.updates: list[dict] = []  # notifications, newest last
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
        while True:
            line = self.process.stdout.readline()
            if not line:
                stderr = self.process.stderr.read() if self.process.stderr else ""
                raise RuntimeError(f"the plugin closed stdout; stderr={stderr!r}")
            message = json.loads(line)
            if "id" in message:
                return message
            # A notification (no id): winshun/settings/update is the plugin
            # handing the page its three keys. It is written during the handling
            # of a request, before that request's response.
            self.updates.append(message)

    def published(self) -> dict:
        """The settings the plugin last handed to the host, or {}."""
        for message in reversed(self.updates):
            if message.get("method") == "winshun/settings/update":
                return (message.get("params") or {}).get("settings") or {}
        return {}

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
                "winshun": "0.30.0",
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

    def query_command(self, command: str, text: str, request_id: int) -> list[dict]:
        answer = self.call(
            "query",
            {"queryId": request_id, "command": command, "trigger": "keyword",
             "text": text, "raw": text, "source": "command", "limit": 200},
            request_id,
        )
        return answer_items(answer)

    def command_action(self, command: str, action_id: str, item: dict,
                       request_id: int) -> list[dict]:
        answer = self.call(
            "action",
            {"command": command, "action": action_id, "text": "",
             "item": item, "trigger": "keyword", "files": []},
            request_id,
        )
        return (answer.get("result") or {}).get("effects") or []

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

            # AI serial I/O is limited to sessions the user shared from the
            # standalone window. Valid COM names alone must not open or probe a
            # serial device, and send is a query plan rather than a write.
            for offset, com in enumerate(("COM9", "COM10"), 0):
                status = plugin.query_command("tio-io", "status", 80 + offset)
                check(any("No user-shared session" in (item.get("title") or "")
                          for item in status),
                      f"tio-io status reports no shared session for {com}")
                read = plugin.query_command("tio-io", f"read {com} --format hex --from 0 --max 16",
                                            90 + offset)
                check(any("not a user-shared session" in (item.get("title") or "")
                          for item in read),
                      f"tio-io read refuses unshared {com}")
                send = plugin.query_command("tio-io", f"send {com} --format ascii --data AT\\r\\n",
                                            100 + offset)
                check(any("not a user-shared session" in (item.get("title") or "")
                          for item in send),
                      f"tio-io send refuses unshared {com}")

            # ---- path A: the grouped answer ----
            items = plugin.query()
            ids = [i.get("id") for i in items]
            check(len(items) >= 4, "the answer carries the lean rows", str(len(items)))
            check(len(launch_rows(items)) >= 1, "every enumerated port has a launch row",
                  str(len(launch_rows(items))))
            check(len({i.get("id") for i in launch_rows(items)}) == len(launch_rows(items)),
                  "the launch rows have distinct ids")
            for wanted in ("baud", "config"):
                check(wanted in ids, f"the {wanted} row is present")
            for gone in ("port", "control", "diagnostics", "openWindow"):
                check(gone not in ids, f"the old {gone} row is gone")

            # The answer is live: the rows are the ports the machine reports now,
            # not the ones it had when the process started, and the host is asked
            # to ask again while the list is shown.
            again = plugin.call(
                "query",
                {"queryId": 3, "command": "tio", "trigger": "keyword", "keyword": "tio",
                 "text": "", "raw": "tio", "limit": 200},
                3,
            )
            rerun = (again.get("result") or {}).get("rerun", 0)
            check(rerun > 0, "the answer asks to be asked again", str(rerun))
            check(answer_items(again) == items, "asking again answers the same rows")
            probe = subprocess.run([str(EXE), "--probe", "ports"], capture_output=True,
                                   text=True, encoding="utf-8")
            enumerated = set()
            for line in probe.stdout.splitlines():
                if not line.startswith("COM"):
                    continue
                head = line[3:].split("\t", 1)[0].split(" ", 1)[0]
                if head.isdigit():
                    enumerated.add(int(head))
            listed = {int(str(i.get("id", ""))[5:]) for i in launch_rows(items)}
            if enumerated:
                check(listed == enumerated, "the rows are the ports the machine has now",
                      f"rows={sorted(listed)} enumerated={sorted(enumerated)}")

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
            check("/dev/ttyS" in (launch.get("subtitle") or ""),
                  "the launch row shows the ttyS mapping", launch.get("subtitle") or "")

            config = find(items, "config")
            check((config.get("actions") or [{}])[0].get("id") == "openWindow",
                  "the config row opens the window")
            check(bool(config.get("subtitle")),
                  "the config row shows the parameters and the backend",
                  config.get("subtitle", ""))

            # ---- opening a candidate list, then going back ----
            bauds = replaced(plugin.action("openBaudrates", find(items, "baud"), 13))
            check(len([i for i in bauds if str(i.get("id", "")).startswith("baud:")]) >= 5,
                  "the baud list offers the supported rates")

            back = replaced(plugin.action("back", {"id": "back"}, 12))
            check(bool(launch_row(back)), "back returns the main list")

            bauds = replaced(plugin.action("openBaudrates", find(items, "baud"), 13))
            check(len([i for i in bauds if str(i.get("id", "")).startswith("baud:")]) >= 5,
                  "the baud list offers the supported rates")

            # ---- choosing from a list announces the page's keys to the host ----
            # The baud rate belongs to the host's settings page: the choice is
            # handed over winshun/settings/update, and tio.json keeps out of it.
            row = find(bauds, "baud:9600")
            check(bool(row), "the baud list has 9600")
            plugin.updates.clear()
            main = replaced(plugin.action("baud", row, 14))
            check(bool(launch_row(main)), "choosing a rate returns the main list")
            handed = plugin.published()
            check(handed.get("baudrate") == 9600,
                  "the rate choice is announced to the host", str(handed))
            saved = json.loads((data_dir / "tio.json").read_text(encoding="utf-8")) \
                if (data_dir / "tio.json").is_file() else {}
            check(saved.get("baudrate") is None and saved.get("port") is None
                  and saved.get("backend") is None,
                  "the page's keys stay out of tio.json", str(sorted(saved)))

            # The report itself lives in the window's diagnostics card and in the
            # host's details pane; the list carries no diagnostics rows.
            details = plugin.call("details", {"item": find(items, "config"), "text": ""}, 34)
            text = (details.get("result") or {}).get("text") or ""
            check("backend" in text, "details reports the backend")
            check("session" in text, "details reports the session")
            check("connected" not in text.lower(), "details never claims a connection")

            # ---- language ----
            plugin.send({"jsonrpc": "2.0", "method": "settingsChanged", "params": {"settings": {}}})
            plugin.send({"jsonrpc": "2.0", "method": "appearance",
                         "params": {"locale": "en", "theme": "dark", "corners": "square"}})
            english = plugin.query(40)
            check(launch_row(english).get("title", "").startswith("tio"),
                  "the launch row survives a language change")
            check(find(english, "baud").get("title") == "Baud rate", "the baud row is localized",
                  find(english, "baud").get("title", ""))

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
                    check(saved.get("port") is None and saved.get("baudrate") is None,
                          "the page's keys are not written into tio.json", str(sorted(saved)))
                    check(saved.get("packageVersion") == "3.9-3",
                          "the configuration records the package version",
                          str(saved.get("packageVersion")))
                    handed = plugin.published()
                    check(str(handed.get("port", "")).startswith("COM"),
                          "the launched port is announced to the host", str(handed))

                    # A session holds its own port and nothing else. On a desk
                    # with a second device attached, that port's row is still a
                    # launch row while the first session runs, and it starts.
                    first_id = running.get("id", "")
                    idle = [row for row in launch_rows(plugin.query(73))
                            if (row.get("actions") or [{}])[0].get("id") == "launchPort"]
                    second_id = idle[0].get("id", "") if idle else ""
                    if idle:
                        check(idle[0].get("valid") is not False,
                              "another port stays launchable while a session runs",
                              idle[0].get("subtitle", ""))
                        second = find(replaced(plugin.action("launchPort", idle[0], 74)),
                                      second_id)
                        check((second.get("actions") or [{}])[0].get("id") == "stopPort",
                              "a second port starts while the first runs",
                              str(second.get("actions") or second))
                        both = [row for row in launch_rows(plugin.query(75))
                                if (row.get("actions") or [{}])[0].get("id") == "stopPort"]
                        check({row.get("id") for row in both} == {first_id, second_id},
                              "both ports run at once", str([row.get("id") for row in both]))

                        stopped_second = replaced(plugin.action("stopPort", second, 76))
                        check((find(stopped_second, second_id).get("actions") or [{}])[0].get("id")
                              == "launchPort", "the second port stops on its own row")
                        check((find(stopped_second, first_id).get("actions") or [{}])[0].get("id")
                              == "stopPort", "stopping one session leaves the other running")

                    time.sleep(1)
                    stopped = plugin.action("stopPort", running, 72)
                    after = launch_row(replaced(stopped))
                    check((after.get("actions") or [{}])[0].get("id") == "launchPort",
                          "after Stop the row offers Launch again",
                          str(after.get("actions")))

            # ---- the standalone window's entries ----
            # The config row (Enter), and Ctrl+Enter on the launch row (the menu).
            entry = find(items, "config")
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
            check(len(again) >= 4, "the protocol still answers with the window open")

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
        # start from it without a window or a session. The page's record comes
        # with it: the host passes what it stored, and the rows follow.
        plugin = Plugin(data_dir)
        try:
            init = plugin.initialize(settings={"baudrate": 9600})
            check("result" in init, "the plugin starts with an existing data folder")
            restored = launch_row(plugin.query(60))
            check(str(restored.get("id", "")).startswith("port:"),
                  "the second run answers the same rows", str(restored.get("id")))
            summary = find(plugin.query(60), "config")
            check("9600" in (summary.get("subtitle") or ""),
                  "the host's record survives a restart", summary.get("subtitle", ""))
        finally:
            plugin.close()

        # The host's settings page owns its three keys: a fresh install is
        # seeded from the page (numbers and the text a settings form sends),
        # and a change from the page wins over any value already in force.
        # Keys the manifest dropped (dataBits, parity) are the plugin's own:
        # the page is only a migration source for them, and tio.json keeps
        # them — the page never sets them back.
        fresh = data_dir / "settings-seed"
        fresh.mkdir()
        plugin = Plugin(fresh)
        try:
            plugin.initialize(settings={"baudrate": 9600, "dataBits": "7", "parity": "even",
                                        "backend": "conhost"})
            seeded = launch_row(plugin.query(61))
            check(str(seeded.get("id", "")).startswith("port:"),
                  "the seeded configuration answers the launch rows", str(seeded.get("id")))
            summary = find(plugin.query(61), "config")
            check("9600" in (summary.get("subtitle") or ""),
                  "the host's settings seed a fresh configuration", summary.get("subtitle", ""))
            check("7e1" in (summary.get("subtitle") or ""),
                  "a numeric setting sent as text is read", summary.get("subtitle", ""))
            check("conhost" in str(summary.get("subtitle") or ""),
                  "the host's backend choice is used", summary.get("subtitle", ""))
            saved = json.loads((fresh / "tio.json").read_text(encoding="utf-8")) \
                if (fresh / "tio.json").is_file() else {}
            check(saved.get("parity") == "even",
                  "a page value the manifest dropped migrates into tio.json",
                  str(saved.get("parity")))
            check(saved.get("baudrate") is None,
                  "the page's baud rate is not copied back into tio.json",
                  str(saved.get("baudrate")))

            # The page changes its baud rate: its record is the user's latest
            # answer, so it applies even over the value already in force. A key
            # the page no longer owns is left alone.
            plugin.send({"jsonrpc": "2.0", "method": "settingsChanged",
                         "params": {"settings": {"baudrate": 300, "parity": "odd"}}})
            plugin.send({"jsonrpc": "2.0", "method": "appearance",
                         "params": {"locale": "zh", "theme": "light"}})
            after = launch_row(plugin.query(66))
            check(str(after.get("id", "")).startswith("port:"),
                  "the rows answer after a settings change", str(after.get("id")))
            summary = find(plugin.query(66), "config")
            check("300" in (summary.get("subtitle") or ""),
                  "a settings change applies over the value in force",
                  summary.get("subtitle", ""))
            check("odd" not in (summary.get("subtitle") or ""),
                  "the page does not set keys it no longer owns", summary.get("subtitle", ""))
        finally:
            plugin.close()

        # The page's record survives a restart: the new process is told the
        # same value and starts from it.
        plugin = Plugin(fresh)
        try:
            plugin.initialize(settings={"baudrate": 300, "backend": "conhost"})
            again = find(plugin.query(67), "config")
            check("300" in (again.get("subtitle") or ""),
                  "the page's changed value survives a restart", again.get("subtitle", ""))
        finally:
            plugin.close()

    print(f"{'PASS' if failed == 0 else 'FAILED'} {total - failed} of {total} protocol checks")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
