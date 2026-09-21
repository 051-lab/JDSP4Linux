#!/usr/bin/env python3
"""Validate and emit the maintained native EEL corpus inventory."""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys


CONTROL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):([^<\r\n]+)<([^,>]+),([^,>]+),([^>{]+)(?:\{([^}]*)\})?>(.*)$")


def entries(root: str, manifest: dict) -> list[dict]:
    bundled = manifest["bundled"]
    result = [
        {"path": path, "expected_compile": bundled["expected_compile"],
         "role": bundled["role"]}
        for path in sorted(glob.glob(os.path.join(root, bundled["glob"])))
    ]
    result += manifest["custom"] + manifest["additional"]
    return result


def inspect(root: str, entry: dict) -> dict:
    path = os.path.normpath(os.path.join(root, entry["path"]))
    if not os.path.isfile(path):
        raise ValueError(f"missing corpus member: {path}")
    text = open(path, encoding="utf-8", newline="").read()
    sections = [name for name in ("@init", "@slider", "@block", "@sample")
                if re.search(rf"^\s*{re.escape(name)}\b", text, re.MULTILINE)]
    controls = []
    for line in text.splitlines():
        match = CONTROL.match(line.strip())
        if match:
            controls.append({
                "name": match.group(1),
                "default": match.group(2),
                "minimum": match.group(3),
                "maximum": match.group(4),
                "step": match.group(5),
                "choices": match.group(6).split(",") if match.group(6) else [],
                "label": match.group(7).strip(),
            })
    has_left = bool(re.search(r"\bspl0\b", text))
    has_right = bool(re.search(r"\bspl1\b", text))
    if has_left and has_right:
        coupling = "stereo-coupled-or-independent"
    elif has_left or has_right:
        coupling = "single-channel"
    else:
        coupling = "channel-agnostic-or-no-direct-sample-reference"
    initialization = {
        "has_init": "@init" in sections,
        "uses_host_rate": bool(re.search(r"\bsrate\b", text)),
        "uses_imported_tables": "importFLTFromStr" in text,
        "uses_large_state_or_stft": bool(re.search(r"\b(stft|FFT|frameLen|memreq)\b", text, re.IGNORECASE)),
    }
    return {
        "path": os.path.relpath(path, root),
        "role": entry["role"],
        "expected_compile": entry["expected_compile"],
        "sections": sections,
        "controls": controls,
        "channel_coupling_hint": coupling,
        "initialization": initialization,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    root = os.path.abspath(os.path.join(os.path.dirname(args.manifest), "../.."))
    with open(args.manifest, encoding="utf-8") as stream:
        manifest = json.load(stream)
    inventory = [inspect(root, entry) for entry in entries(root, manifest)]
    if len(inventory) != 50:
        raise ValueError(f"expected 50 corpus entries, found {len(inventory)}")
    if any(not item["role"] or not item["expected_compile"] for item in inventory):
        raise ValueError("every corpus entry requires role and expected_compile")
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as stream:
        json.dump({"entries": inventory}, stream, indent=2)
        stream.write("\n")
    print(f"EEL corpus inventory passed: {len(inventory)} files")
    print(f"controls={sum(len(item['controls']) for item in inventory)}")
    print(f"expected_pass={sum(item['expected_compile'] == 'pass' for item in inventory)}")
    print(f"expected_reject={sum(item['expected_compile'] == 'fail' for item in inventory)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"inventory error: {error}", file=sys.stderr)
        raise SystemExit(1)
