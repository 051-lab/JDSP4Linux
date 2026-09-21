#!/usr/bin/env python3
"""Run the native LiveProg smoke test for the maintained corpus manifest."""
from __future__ import annotations

import glob
import argparse
import json
import os
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest")
    parser.add_argument("native_test")
    parser.add_argument("--skip-external", action="store_true")
    parser.add_argument("--inventory")
    args = parser.parse_args()
    manifest_path, native_test = args.manifest, args.native_test
    root = os.path.abspath(os.path.join(os.path.dirname(manifest_path), "../.."))
    with open(manifest_path, encoding="utf-8") as stream:
        manifest = json.load(stream)

    entries = [{"path": path, "expected_compile": manifest["bundled"]["expected_compile"]}
               for path in sorted(glob.glob(os.path.join(root, manifest["bundled"]["glob"]))) ]
    entries += manifest["custom"] + manifest["additional"]
    if args.skip_external:
        entries = [entry for entry in entries if not entry["path"].startswith("../")]
    paths = [os.path.normpath(os.path.join(root, entry["path"])) for entry in entries]
    missing = [path for path in paths if not os.path.isfile(path)]
    if missing:
        for path in missing:
            print(f"missing corpus member: {path}", file=sys.stderr)
        return 1
    print(f"running native corpus: {len(paths)} files")
    inventory = {}
    if args.inventory:
        with open(args.inventory, encoding="utf-8") as stream:
            inventory = {item["path"]: item for item in json.load(stream)["entries"]}
    for path, entry in zip(paths, entries):
        command = [native_test, path]
        metadata = inventory.get(os.path.relpath(path, root), {})
        controls = metadata.get("controls", [])
        if controls and entry["expected_compile"] == "pass":
            command.append("--controls")
            for control in controls:
                command.extend([control["name"], control["default"],
                                control["minimum"], control["maximum"]])
        result = subprocess.run(command, cwd=root, timeout=120, check=False)
        expected_pass = entry["expected_compile"] == "pass"
        observed_pass = result.returncode == 0
        if observed_pass != expected_pass:
            print(f"unexpected native result for {path}: returncode={result.returncode}", file=sys.stderr)
            return result.returncode or 1
        outcome = "passed" if expected_pass else "rejected as expected"
        print(f"{outcome}: {os.path.relpath(path, root)}")
    print(f"native EEL corpus expectations passed: {len(paths)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
