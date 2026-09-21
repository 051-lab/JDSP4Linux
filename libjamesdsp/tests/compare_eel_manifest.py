#!/usr/bin/env python3
"""Compare Python structural validation with the native corpus result."""
from __future__ import annotations

import glob
import json
import os
import subprocess
import sys
import argparse
from pathlib import Path

workspace = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(workspace))
from projects.eelvault.eel_parse import parse_file  # noqa: E402
from projects.eelvault.validate import is_valid, validate  # noqa: E402


def entries(root: Path, manifest: dict) -> list[dict]:
    result = [
        {"path": path, "expected_compile": manifest["bundled"]["expected_compile"],
         "role": manifest["bundled"]["role"]}
        for path in sorted(glob.glob(str(root / manifest["bundled"]["glob"])))
    ]
    result.extend(manifest["custom"])
    result.extend(manifest["additional"])
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest")
    parser.add_argument("native_test")
    parser.add_argument("output_json", nargs="?")
    parser.add_argument("--skip-external", action="store_true")
    args = parser.parse_args()
    manifest_path = Path(args.manifest).resolve()
    native_test = args.native_test
    output_path = Path(args.output_json) if args.output_json else None
    root = manifest_path.parents[2]
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    records = []
    for entry in entries(root, manifest):
        if args.skip_external and entry["path"].startswith("../"):
            continue
        path = Path(os.path.normpath(os.path.join(root, entry["path"])))
        issues = validate(parse_file(str(path)))
        native = subprocess.run([native_test, str(path)], cwd=root,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                 timeout=120, check=False)
        record = {
            "path": os.path.relpath(path, root),
            "python_valid": is_valid(issues),
            "python_errors": [str(issue) for issue in issues if issue.level == "error"],
            "native_pass": native.returncode == 0,
            "expected_native_pass": entry["expected_compile"] == "pass",
            "role": entry.get("role", ""),
        }
        records.append(record)
    output = {"files": records, "count": len(records),
              "native_expected_pass": all(r["native_pass"] == r["expected_native_pass"]
                                           for r in records)}
    if output_path:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    python_native_differences = [r for r in records if r["python_valid"] != r["native_pass"]]
    print(f"manifest comparison passed: {len(records)} files; "
          f"python/native differences: {len(python_native_differences)}")
    for record in python_native_differences:
        print(f"difference: {record['path']} "
              f"python_valid={record['python_valid']} native_pass={record['native_pass']}")
    return 0 if output["native_expected_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
