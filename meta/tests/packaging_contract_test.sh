#!/usr/bin/env bash
set -euo pipefail

script_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_root=$(cd "$script_root/.." && pwd)
build_root=${BUILD_ROOT:-"$repo_root/../../build/jamesdsp-t18-packaging"}
fake_bin=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
log_file="$build_root/fake-flatpak.log"

mkdir -p "$build_root/src"
rm -f "$log_file"

bash -n "$script_root/flatpak/build-local-bundle.sh" "$script_root/build_deb_package.sh"

if "$script_root/build_deb_package.sh" >/dev/null 2>&1; then
  printf 'Debian packager accepted missing arguments\n' >&2
  exit 1
fi
if "$script_root/build_deb_package.sh" 1.0 unknown >/dev/null 2>&1; then
  printf 'Debian packager accepted an unknown flavor\n' >&2
  exit 1
fi

deb_failure_root=$(mktemp -d "$repo_root/../../build/jamesdsp-t18-deb-failure.XXXXXX")
trap 'rm -rf "$deb_failure_root"' EXIT
if (cd "$deb_failure_root" && "$script_root/build_deb_package.sh" 1.0 pulse >/dev/null 2>&1); then
  printf 'Debian packager accepted missing package inputs\n' >&2
  exit 1
fi
if find "$deb_failure_root" -mindepth 1 -maxdepth 1 -type d -print -quit | grep -q .; then
  printf 'Debian packager left partial staging after preflight failure\n' >&2
  exit 1
fi
collision_root=$(mktemp -d "$repo_root/../../build/jamesdsp-t18-deb-collision.XXXXXX")
mkdir -p "$collision_root/resources/icons" "$collision_root/jamesdsp-pulse_1.0_ubuntu22-04_amd64"
printf 'sentinel\n' > "$collision_root/jamesdsp-pulse_1.0_ubuntu22-04_amd64/sentinel"
cp /usr/bin/true "$collision_root/jamesdsp"
cp "$repo_root/resources/icons/icon.png" "$collision_root/resources/icons/icon.png"
cp "$repo_root/LICENSE" "$collision_root/LICENSE"
if (cd "$collision_root" && "$script_root/build_deb_package.sh" 1.0 pulse >/dev/null 2>&1); then
  printf 'Debian packager accepted an existing staging path\n' >&2
  exit 1
fi
grep -Fx 'sentinel' "$collision_root/jamesdsp-pulse_1.0_ubuntu22-04_amd64/sentinel" >/dev/null
mkdir -p "$collision_root/jamesdsp-pulse_1.0_ubuntu22-04_amd64.deb"
if (cd "$collision_root" && "$script_root/build_deb_package.sh" 1.0 pulse >/dev/null 2>&1); then
  printf 'Debian packager accepted an existing package output\n' >&2
  exit 1
fi
test -d "$collision_root/jamesdsp-pulse_1.0_ubuntu22-04_amd64.deb"
if "$script_root/build_deb_package.sh" '1.0/bad' pulse >/dev/null 2>&1; then
  printf 'Debian packager accepted an unsafe version\n' >&2
  exit 1
fi

bundle_output=$(PACKAGING_FAKE_LOG="$log_file" PATH="$fake_bin:$PATH" \
  BUILD_ROOT="$build_root" JAMESDSP_BINARY=/usr/bin/true \
  FLATPAK_RUNTIME=org.example.Test/runtime \
  "$script_root/flatpak/build-local-bundle.sh" \
  "$build_root/test.flatpak")

grep -F -- '--filesystem=/usr/bin:ro' "$log_file" >/dev/null
grep -F -- '--env=binary=/usr/bin/true' "$log_file" >/dev/null
expected_hash=$(sha256sum /usr/bin/true | awk '{print $1}')
grep -F -- "Binary SHA256: $expected_hash (/usr/bin/true)" <<<"$bundle_output" >/dev/null
grep -F -- 'install -Dm755 "$binary" /app/bin/jamesdsp' "$script_root/flatpak/build-local-bundle.sh" >/dev/null
grep -F -- 'set -eu' "$script_root/flatpak/build-local-bundle.sh" >/dev/null
grep -F -- '../../build/jamesdsp/' "$script_root/flatpak/LOCAL_BUILD.md" >/dev/null

space_root=$(mkdir -p "$repo_root/../../build/jamesdsp t18 spaces" && cd "$repo_root/../../build/jamesdsp t18 spaces" && pwd)
mkdir -p "$space_root/src"
cp /usr/bin/true "$space_root/src/jamesdsp"
space_log="$space_root/fake-flatpak.log"
space_output=$(PACKAGING_FAKE_LOG="$space_log" PATH="$fake_bin:$PATH" \
  BUILD_ROOT="$space_root" JAMESDSP_BINARY="$space_root/src/jamesdsp" \
  FLATPAK_RUNTIME=org.example.Test/runtime \
  "$script_root/flatpak/build-local-bundle.sh" \
  "$space_root/space output.flatpak")
grep -F -- "--filesystem=$space_root/src:ro" "$space_log" >/dev/null
grep -F -- "--env=binary=$space_root/src/jamesdsp" "$space_log" >/dev/null
space_hash=$(sha256sum "$space_root/src/jamesdsp" | awk '{print $1}')
grep -F -- "Binary SHA256: $space_hash ($space_root/src/jamesdsp)" <<<"$space_output" >/dev/null

custom_binary="$build_root/src/custom-jamesdsp"
printf '#!/bin/sh\nexit 0\n' > "$custom_binary"
chmod 755 "$custom_binary"
custom_output=$(PACKAGING_FAKE_LOG="$log_file" PATH="$fake_bin:$PATH" \
  BUILD_ROOT="$build_root" JAMESDSP_BINARY="$custom_binary" \
  FLATPAK_RUNTIME=org.example.Test/runtime \
  "$script_root/flatpak/build-local-bundle.sh" \
  "$build_root/custom.flatpak")
grep -F -- "--filesystem=$build_root/src:ro" "$log_file" >/dev/null
grep -F -- "--env=binary=$custom_binary" "$log_file" >/dev/null
custom_hash=$(sha256sum "$custom_binary" | awk '{print $1}')
grep -F -- "Binary SHA256: $custom_hash ($custom_binary)" <<<"$custom_output" >/dev/null

failure_root="$repo_root/../../build/jamesdsp-t18-packaging-failure"
mkdir -p "$failure_root/src"
if PACKAGING_FAKE_LOG="$failure_root/fake-flatpak.log" PATH="$fake_bin:$PATH" \
    BUILD_ROOT="$failure_root" JAMESDSP_BINARY=/usr/bin/true \
    FLATPAK_RUNTIME=org.example.Test/runtime PACKAGING_FAKE_FAIL_BUILD=1 \
    "$script_root/flatpak/build-local-bundle.sh" \
    "$failure_root/should-not-be-created.flatpak" >/dev/null 2>&1; then
  printf 'Flatpak packager masked a staging failure\n' >&2
  exit 1
fi

printf 'packaging contract test passed\n'
