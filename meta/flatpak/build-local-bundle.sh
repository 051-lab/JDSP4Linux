#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build_root=${BUILD_ROOT:-"$repo_root/../../build/jamesdsp"}
binary=${JAMESDSP_BINARY:-"$build_root/src/jamesdsp"}
runtime=${FLATPAK_RUNTIME:-org.kde.Platform//6.10}
branch=${FLATPAK_BRANCH:-custom-liveprog}
extra_filesystems=${FLATPAK_EXTRA_FILESYSTEMS:-}

if [[ ! -d "$build_root" ]]; then
	printf 'Missing build directory: %s\n' "$build_root" >&2
	exit 1
fi
build_root=$(cd "$build_root" && pwd)
binary=${JAMESDSP_BINARY:-"$build_root/src/jamesdsp"}
if [[ "$binary" != /* ]]; then
	binary=$(cd "$(dirname "$binary")" && pwd)/$(basename "$binary")
fi
binary_dir=$(cd "$(dirname "$binary")" && pwd)
output=${1:-"$build_root/jamesdsp-liveprog-${branch}.flatpak"}

if [[ ! -x "$binary" ]]; then
	printf 'Missing executable: %s\nBuild the application first.\n' "$binary" >&2
	exit 1
fi
binary_sha256=$(sha256sum "$binary" | awk '{print $1}')

if ! flatpak info "$runtime" >/dev/null 2>&1; then
	printf 'Required runtime is not installed: %s\n' "$runtime" >&2
	exit 1
fi

mkdir -p "$(dirname "$output")"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/jamesdsp-flatpak.XXXXXX")
lib_archive="$work_dir/app-libs.tar"
stage="$work_dir/stage"
repo="$work_dir/repo"
trap 'rm -rf "$work_dir"' EXIT

# The stable Flathub package supplies the glibmm/sigc++ libraries that are not
# part of the KDE runtime. The branch is explicitly selected so a user-local
# custom branch cannot become its own dependency source.
flatpak run --branch=stable --command=tar me.timschneeberger.jdsp4linux \
	-C /app/lib -cf - . >"$lib_archive"

flatpak build-init --arch=x86_64 "$stage" \
	me.timschneeberger.jdsp4linux "$runtime" "$runtime"

flatpak build --filesystem="$repo_root:ro" --filesystem="$build_root/src:ro" \
	--filesystem="$binary_dir:ro" \
	--filesystem="$lib_archive:ro" --env=repo_root="$repo_root" \
	--env=build_root="$build_root" --env=binary="$binary" \
	--env=lib_archive="$lib_archive" \
	"$stage" sh -c \
	'set -eu
	 install -Dm755 "$binary" /app/bin/jamesdsp
	 install -Dm644 "$repo_root/meta/flatpak/me.timschneeberger.jdsp4linux.desktop" \
		/app/share/applications/me.timschneeberger.jdsp4linux.desktop
	 install -Dm644 "$repo_root/meta/flatpak/jamesdsp.svg" \
		/app/share/icons/hicolor/scalable/apps/me.timschneeberger.jdsp4linux.svg
	 install -Dm644 "$repo_root/LICENSE" \
		/app/share/licenses/me.timschneeberger.jdsp4linux/LICENSE
	 install -Dm644 "$repo_root/meta/flatpak/me.timschneeberger.jdsp4linux.metainfo.xml" \
		/app/share/metainfo/me.timschneeberger.jdsp4linux.metainfo.xml
	 mkdir -p /app/lib
	 tar -xf "$lib_archive" -C /app/lib
	 test -x /app/bin/jamesdsp
	 test -f /app/share/applications/me.timschneeberger.jdsp4linux.desktop
	 test -f /app/share/icons/hicolor/scalable/apps/me.timschneeberger.jdsp4linux.svg
	 test -f /app/share/licenses/me.timschneeberger.jdsp4linux/LICENSE
	 test -f /app/share/metainfo/me.timschneeberger.jdsp4linux.metainfo.xml'

finish_args=(
	--command=jamesdsp
	--share=network --share=ipc \
	--socket=x11 --socket=wayland --socket=fallback-x11 \
	--device=dri \
	--filesystem=xdg-run/pipewire-0:ro \
	--filesystem=xdg-config/kdeglobals:ro \
	--talk-name=com.canonical.AppMenu.Registrar \
	--talk-name=org.kde.KGlobalSettings \
	--talk-name=org.kde.StatusNotifierWatcher \
	--talk-name=org.kde.kconfig.notify
)
if [[ -n "$extra_filesystems" ]]; then
	read -r -a filesystem_entries <<< "$extra_filesystems"
	for filesystem in "${filesystem_entries[@]}"; do
		finish_args+=("--filesystem=$filesystem")
	done
fi
flatpak build-finish "$stage" "${finish_args[@]}"

flatpak build-export --arch=x86_64 "$repo" "$stage" "$branch"
flatpak build-bundle "$repo" "$output" \
	me.timschneeberger.jdsp4linux "$branch" \
	--runtime-repo=https://flathub.org/repo/flathub.flatpakrepo

printf 'Created %s\n' "$output"
printf 'Binary SHA256: %s (%s)\n' "$binary_sha256" "$binary"
printf 'Install with: flatpak install --user --assumeyes %q\n' "$output"
