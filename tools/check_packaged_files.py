"""Fail if any file packaging needs is not tracked in git.

CI checks out the tag and runs tools/package.py there, so every file the
packager reads must be committed: BASE_FILES, manifest/files.json paths, the
licence files third-party.json links to, and the licence globs.
"""
from __future__ import annotations

import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def tracked() -> set[str]:
    out = subprocess.check_output(["git", "ls-files"], cwd=ROOT)
    return set(out.decode().splitlines())


def main() -> int:
    committed = tracked()
    needed: set[str] = set()
    import importlib.util
    spec = importlib.util.spec_from_file_location("pkg", ROOT / "tools" / "package.py")
    package = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package)
    needed.update(package.BASE_FILES)
    needed.update(entry["path"] for entry in
                  json.loads((ROOT / "manifest" / "files.json").read_text(encoding="utf-8"))["files"])
    third = json.loads((ROOT / "manifest" / "third-party.json").read_text(encoding="utf-8"))
    for component in third["components"]:
        needed.update(component.get("licenseFiles", []))
    for licence in (ROOT / "licenses").iterdir():
        if licence.is_file():
            needed.add(f"licenses/{licence.name}")

    # The two binaries are built per configuration (locally or in CI via
    # --artifact-dir); the checker treats the built binaries as present.
    missing = sorted(n for n in needed if n not in committed
                     and not (ROOT / n).is_file())
    if missing:
        print("not tracked in git:")
        for name in missing:
            print(" ", name)
        return 1
    print(f"all {len(needed)} packaged files are tracked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
