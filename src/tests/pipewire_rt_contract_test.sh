#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_file="$repo_dir/audio/pipewire/PwBasePlugin.cpp"
body="$(sed -n '/void on_process(/,/^}/p' "$source_file")"

grep -Fq 'format_change_dispatcher.emit();' <<<"$body"
grep -Fq 'format_ready.load' <<<"$body"
! grep -Eq '\.(resize|reserve)\s*\(' <<<"$body"
! grep -Fq -- '->setup();' <<<"$body"

echo "PipeWire RT callback contract passed"
