#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_file="$repo_dir/audio/pulseaudio/wrapper/gstjamesdsp.c"
body="$(sed -n '/gst_jamesdsp_transform_ip(/,/^}/p' "$source_file")"
setup_body="$(sed -n '/gst_jamesdsp_setup(/,/^}/p' "$source_file")"

grep -Fq 'if (!gst_buffer_map(buf, &map, GST_MAP_READWRITE))' <<<"$body"
grep -Fq 'gst_audio_format_to_string' <<<"$setup_body"
grep -Fq 'self->format = 0' <<<"$setup_body"
grep -Fq 'self->format = 2' <<<"$setup_body"
! grep -Fq 'gst_pad_get_current_caps' <<<"$body"
! grep -Eq '\b(malloc|free)\s*\(' <<<"$body"

echo "PulseAudio wrapper contract passed"
