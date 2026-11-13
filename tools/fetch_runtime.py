"""Fetch the bundled tio runtime into runtime/ before packaging.

The package ships tio 3.9 and the MSYS2 DLLs it imports, and tools/package.py
refuses to pack a runtime that does not verify against manifest/files.json.
That payload is distribution data, not source, so it stays out of git: CI (or a
fresh checkout) restores it from the --from-release .wsplugin asset, and every
file is verified against the sha256 recorded in manifest/files.json either way.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import pathlib
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def load_manifest() -> list[dict]:
    manifest = json.loads((ROOT / "manifest" / "files.json").read_text(encoding="utf-8"))
    return manifest["files"]


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=180) as response:
        return response.read()


def verify(existing: dict[str, bytes]) -> list[str]:
    """Return the manifest paths whose bytes are present and hash-verified."""
    ok = []
    for entry in load_manifest():
        data = existing.get(entry["path"])
        if data is not None and hashlib.sha256(data).hexdigest() == entry["sha256"]:
            ok.append(entry["path"])
    return ok


def write_runtime(existing: dict[str, bytes]) -> None:
    for entry in load_manifest():
        data = existing.get(entry["path"])
        if data is None:
            raise SystemExit(f"the runtime source lacks {entry['path']}")
        if hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise SystemExit(f"{entry['path']} does not match the recorded sha256")
        target = ROOT / entry["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        print(f"runtime/{entry['path'].removeprefix('runtime/')}: {len(data)} bytes, verified")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--from-release",
        metavar="URL",
        help="a previously published .wsplugin asset to restore the runtime from",
    )
    args = parser.parse_args()

    paths = [entry["path"] for entry in load_manifest()]

    current: dict[str, bytes] = {}
    for path in paths:
        file = ROOT / path
        if file.is_file():
            current[path] = file.read_bytes()
    if verify(current) == paths:
        print("runtime already present and verified against manifest/files.json")
        return 0

    if not args.from_release:
        raise SystemExit(
            "runtime/ is incomplete and no --from-release source was given; "
            "run from a machine that has the bundled runtime, or pass a .wsplugin asset URL"
        )

    print(f"restoring the runtime from {args.from_release}")
    payload = fetch(args.from_release)
    restored: dict[str, bytes] = {}
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        for path in paths:
            try:
                restored[path] = archive.read(path)
            except KeyError:
                raise SystemExit(f"the source package does not carry {path}")
    write_runtime(restored)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
