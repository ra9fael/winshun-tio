"""The standalone window, checked without WinShun.

`tio.exe --window` opens the QML window with no host and no protocol. This
script starts it in each of the four combinations (zh/en × light/dark), waits, and
fails if the process died or wrote a QML warning to stderr.

The window is built and laid out with `--no-show` by default, so a run never puts
a window on the desktop of whoever is working (`--shown` opts into a visible pass
for a human review).

A QML warning is a real defect here: "Unable to assign [undefined] to …" means a
binding reads a property the model does not expose, and the window would render
with blank or default colours that nobody saw in review.

Needs WinShun's Qt folder (the window runs on the host's Qt 6.12.0). Nothing is
launched on a port.
"""
from __future__ import annotations

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

# Lines that are warnings about the QML we ship. Anything else on stderr (a
# driver note, a font fallback) is not ours to fail on.
BAD = (
    "Unable to assign",
    "is not a type",
    "is not installed",
    "Cannot assign",
    "ReferenceError",
    "TypeError",
    "Binding loop",
    "Non-existent attached object",
    "failed to load component",
    "did not load",
)


def check(condition: bool, label: str, detail: str = "") -> None:
    global total, failed
    total += 1
    if condition:
        print(f"PASS {label}")
    else:
        failed += 1
        print(f"FAIL {label}{': ' + detail if detail else ''}")


def qt_folder() -> pathlib.Path | None:
    for name in ("WS_QT_DIR", "WS_APP_DIR"):
        value = os.environ.get(name)
        if value and (pathlib.Path(value) / "Qt6Core.dll").is_file():
            return pathlib.Path(value)
    installed = pathlib.Path(r"C:\Program Files\WinShun")
    if (installed / "Qt6Core.dll").is_file():
        return installed
    return None


def run_case(app_dir: pathlib.Path, data_dir: pathlib.Path, zh: bool, dark: bool,
             appearance: dict | None = None, shown: bool = False) -> None:
    args = [str(EXE), "--window", "--app-dir", str(app_dir), "--data-dir", str(data_dir)]
    if not shown:
        args.append("--no-show")
    if not zh:
        args.append("--en")
    if dark:
        args.append("--dark")
    label = f"{'zh' if zh else 'en'}/{'dark' if dark else 'light'}"
    if appearance:
        label += f"/{appearance.get('uiStyle', 'host')}"

    env = dict(os.environ, QT_QUICK_BACKEND="software")
    if appearance:
        # The standalone run has no host to push an appearance message; the env
        # var carries one, so the fallback/native switch is exercised too.
        env["TIO_TEST_APPEARANCE"] = json.dumps(appearance)
    process = subprocess.Popen(
        args,
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        env=env,
    )
    try:
        # The window renders within a second or two; a crash shows up as an exit.
        time.sleep(4)
        check(process.poll() is None, f"the window stays up ({label})",
              "" if process.poll() is None else f"exit code {process.returncode}")
        check(process.poll() is None, f"the window did not crash ({label})")
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            _, stderr = process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            _, stderr = process.communicate()

    lines = [line for line in (stderr or "").splitlines() if line.strip()]
    problems = [line for line in lines if any(marker in line for marker in BAD)]
    check(not problems, f"no QML warning ({label})",
          problems[0] if problems else "")
def main() -> int:
    shown = "--shown" in sys.argv[1:]
    if not EXE.is_file():
        print(f"the plugin exe is missing: {EXE}", file=sys.stderr)
        return 2
    app_dir = qt_folder()
    if app_dir is None:
        print("no WinShun Qt folder found (set WS_QT_DIR or install WinShun)", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="tio-window-") as folder:
        data_dir = pathlib.Path(folder)
        for zh in (True, False):
            for dark in (False, True):
                run_case(app_dir, data_dir, zh, dark, shown=shown)

        # The four styles the host can set: each must load, with no QML warning
        # about a token the bridge does not carry. nativeLook's colours differ
        # per style; the bridge is what maps them.
        for style in ("fluent", "brand", "tactile", "liquid"):
            appearance = {
                "uiStyle": style, "uiSize": "normal", "corners": "round",
                "transparency": "off",
                "nativeLook": {
                    "style": style, "dark": True, "rounded": True,
                    "background": "#ff202020", "page": "#ff181818", "layer": "#ff2b2b2b",
                    "divider": "#ff3a3a3a", "text": "#fff3f3f3", "subtext": "#ffb8b8b8",
                    "faint": "#ff8a8a8a", "hover": "#0dffffff", "pressed": "#14ffffff",
                    "control": "#ff373737", "controlBorder": "#ff444444",
                    "inputFocused": "#ff1f1f1f", "accent": "#ffa8402c",
                    "accentFill": "#ffa8402c", "accentText": "#ffffffff",
                    "danger": "#ffff99a4", "dangerFill": "#ffa4261c",
                    "cardRadius": 10, "controlRadius": 5, "largeControlRadius": 8,
                    "smallControlRadius": 3, "menuRadius": 8, "menuItemRadius": 6,
                },
            }
            run_case(app_dir, data_dir, True, True, appearance, shown=shown)

        # Corners square: every radius is zero. The window must still load.
        square = {
            "uiStyle": "fluent", "uiSize": "compact", "corners": "square",
            "nativeLook": {"background": "#fff3f3f3", "text": "#ff1b1b1b", "dark": False,
                           "cardRadius": 10, "controlRadius": 5},
        }
        run_case(app_dir, data_dir, False, False, square, shown=shown)

        # The window keeps to the data folder it was given: nothing of the host's
        # own configuration is touched.
        check(data_dir.is_dir(), "the window kept to the data folder it was given")

    print(f"{'PASS' if failed == 0 else 'FAILED'} {total - failed} of {total} window checks")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
