"""Deterministic .wsplugin package for ra9fael.tio.

--dev packages the working tree (uncommitted changes included) for local
testing. Release mode uses git-tracked files, and --tag additionally requires a
clean worktree with a matching CHANGELOG section. Never publishes.

The archive is byte-identical for identical inputs: fixed timestamps, sorted
entry order, stored (not deflated) compression.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import pathlib
import re
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

# Everything the plugin ships beside its compiled exe and TioApp.dll. The runtime
# payload comes from manifest/files.json so the list cannot silently drift from
# what was verified at runtime.
BASE_FILES = [
    "plugin.json",
    "tio.exe",
    "TioApp.dll",
    "app.ico",
    "tio.rc",
    "icons/app-16.png",
    "icons/app-20.png",
    "icons/app-24.png",
    "icons/app-32.png",
    "icons/app-40.png",
    "icons/app-48.png",
    "icons/app-64.png",
    "icons/app-256.png",
    "icons/PHOSPHOR-LICENSE",
    "LICENSE",
    "README.md",
    "README.en.md",
    "CHANGELOG.md",
    "CHANGELOG.en.md",
    "THIRD_PARTY.md",
    "THIRD_PARTY.en.md",
    "docs/implementation.md",
    "docs/implementation.en.md",
    "docs/validation.md",
    "docs/validation.en.md",
    "manifest/files.json",
    "manifest/third-party.json",
    "CMakeLists.txt",
    "winshun.h",
    "tio.rc",
    "qt_window_icon.h",
    "logic.h",
    "native.h",
    "controller.h",
    "core.h",
    "rows.h",
    "rows_build.h",
    "plugin.h",
    "probe.h",
    "window_hook.h",
    "snapshot.h",
    "qmlapp/qmlmain.cpp",
    "qmlapp/ThemeBridge.cpp",
    "qmlapp/ThemeBridge.h",
    "qmlapp/TioWindow.h",
    "qmlapp/TioWindow.cpp",
    "qmlapp/ShareClient.h",
    "qmlapp/ShareClient.cpp",
    "qmlapp/AnsiText.h",
    "qmlapp/AnsiText.cpp",
    "qmlapp/stub/main.cpp",
    "qmlapp/qml/TioWindow.qml",
    "qmlapp/qml/DesignTokens.qml",
    "qmlapp/qml/TokenComboBox.qml",
    "qmlapp/qml/TokenSegmented.qml",
    "qmlapp/qml/TokenButton.qml",
    "qmlapp/qml/GroupCard.qml",
    "qmlapp/qml/FieldRow.qml",
    "qmlapp/qml/DetailLine.qml",
    "qmlapp/qml/LinkLabel.qml",
    "tests/test_logic.cpp",
    "tests/native_probe.cpp",
    "tests/integration.py",
    "tests/host_smoke.py",
    "tests/window_check.py",
    "tests/test_window.cpp",
    "tools/package.py",
    "tools/copy_qt_dlls.cmake",
    "tools/fetch_runtime.py",
    "tools/remote_share_gui.ps1",
    "tools/probe_share.py",
    "tools/probe_share_client.cpp",
    "tools/probe_ansi.cpp",
    "tools/probe_share_view.py",
    "tools/probe_wheel.py",
    "tools/probe_click.py",
    "tools/probe_clipboard.py",
    "tools/probe_clipboard_perm.py",
    "tools/check_share_socket.py",
    "tests/test_share_client.cpp",
    "tests/test_ansi_text.cpp",
]


def version_section(filename: str, version: str) -> str:
    text = (ROOT / filename).read_text(encoding="utf8")
    section = re.search(
        r"^## \[" + re.escape(version) + r"\][^\n]*(?:\n(?!## \[)[^\n]*)*", text, re.MULTILINE
    )
    if not section:
        raise ValueError(f"missing release changelog section: {filename}")
    return section.group(0).strip()


def runtime_files() -> list[str]:
    manifest = json.loads((ROOT / "manifest" / "files.json").read_text(encoding="utf8"))
    return [entry["path"] for entry in manifest["files"]]


def licence_files() -> list[str]:
    """Every licence text in licenses/, by glob: a new one must not need an edit."""
    return sorted(f"licenses/{path.name}" for path in (ROOT / "licenses").iterdir()
                  if path.is_file())


def third_party_files() -> list[str]:
    """The licence texts the third-party record links to must really be here."""
    record = json.loads((ROOT / "manifest" / "third-party.json").read_text(encoding="utf8"))
    wanted: set[str] = set()
    for component in record["components"]:
        wanted.update(component.get("licenseFiles", []))
    missing = sorted(path for path in wanted if not (ROOT / path).is_file())
    if missing:
        raise ValueError("the third-party record links to files that are not there: "
                         + ", ".join(missing))
    return sorted(wanted)


def archive(names: list[str], overrides: dict[str, bytes] | None = None) -> bytes:
    output = io.BytesIO()
    overrides = overrides or {}
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as zf:
        for name in sorted(set(names)):
            source = (ROOT / name).resolve()
            if not source.is_relative_to(ROOT.resolve()) or (not source.is_file() and name not in overrides):
                raise ValueError(f"invalid package source: {name}")
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            zf.writestr(info, overrides[name] if name in overrides else source.read_bytes())
    return output.getvalue()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dev", action="store_true", help="package the working tree")
    parser.add_argument("--tag", help="release tag, must be v<version>")
    parser.add_argument("--out", type=pathlib.Path, default=ROOT / "dist" / "development")
    parser.add_argument(
        "--artifact-dir",
        type=pathlib.Path,
        help="directory containing the freshly built tio.exe and TioApp.dll",
    )
    args = parser.parse_args()

    manifest = json.loads((ROOT / "plugin.json").read_text(encoding="utf8"))
    version = manifest["version"]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError(f"invalid version: {version}")
    if args.dev and args.tag:
        raise ValueError("--dev cannot be combined with --tag")

    if args.tag:
        if args.tag != "v" + version:
            raise ValueError("the tag does not match plugin.json")
        # Tracked changes only: an untracked draft next to the source must not
        # block a release, and it is never packaged either (BASE_FILES is a
        # whitelist).
        status = subprocess.check_output(
            ["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT
        ).strip()
        if status:
            raise ValueError("a release requires a clean worktree")
        for name in ("CHANGELOG.md", "CHANGELOG.en.md"):
            version_section(name, version)

    # Both binaries must exist before anything is packaged: shipping a package
    # the host cannot start is worse than failing here.
    artifact_dir = (args.artifact_dir or ROOT).resolve()
    binaries = {}
    for name in ("tio.exe", "TioApp.dll"):
        source = artifact_dir / name
        if not source.is_file():
            raise ValueError(f"missing build output: {source} (build first)")
        binaries[name] = source

    # The runtime payload must verify before anything is packaged: shipping a
    # package the launcher would refuse is worse than failing here.
    probe = subprocess.run(
        [str(binaries["tio.exe"]), "--probe", "verify", str(ROOT)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    if probe.returncode != 0:
        raise ValueError("the packaged runtime does not verify:\n" + probe.stdout + probe.stderr)

    names = BASE_FILES + runtime_files() + licence_files() + third_party_files()
    # Keep the archive rooted in the source tree while allowing an isolated
    # build to supply the two binaries without touching existing root outputs.
    payload = archive(names, {name: source.read_bytes() for name, source in binaries.items()
                              if source.resolve() != (ROOT / name).resolve()})
    digest = hashlib.sha256(payload).hexdigest()

    args.out.mkdir(parents=True, exist_ok=True)
    target = args.out / f"ra9fael.tio-{version}.wsplugin"
    target.write_bytes(payload)
    (args.out / f"ra9fael.tio-{version}.wsplugin.sha256").write_text(
        f"{digest}  {target.name}\n", encoding="utf8"
    )
    print(f"{target}  {len(payload)} bytes")
    print(f"sha256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
