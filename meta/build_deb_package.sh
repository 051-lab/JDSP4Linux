#!/usr/bin/env bash
set -euo pipefail

# Binary, resources/icons/icon.png, and LICENSE must be in the working
# directory. The output directory is intentionally not removed if it already
# exists; callers must choose a fresh staging directory.

if [[ $# -ne 2 ]]; then
  printf 'Usage: %s VERSION FLAVOR\n' "$0" >&2
  exit 2
fi

version=$1
flavor=$2

if [[ -z "$version" || ! "$version" =~ ^[0-9A-Za-z.+:~_-]+$ ]]; then
  printf 'ERROR: Invalid package version: %s\n' "$version" >&2
  exit 2
fi

for required_file in jamesdsp resources/icons/icon.png LICENSE; do
  if [[ ! -f "$required_file" || ! -r "$required_file" ]]; then
    printf 'ERROR: Required package input is missing or unreadable: %s\n' "$required_file" >&2
    exit 1
  fi
done

conflict=""
deps=""
if [[ "$flavor" == "pipewire" ]]; then
   conflict="jamesdsp-pulse"
   deps="libarchive13, qt6-qpa-plugins (>= 6.2.4), libqt6gui6 (>= 6.2.4), libqt6dbus6 (>= 6.2.4), libqt6widgets6  (>= 6.2.4), libqt6svgwidgets6 (>= 6.2.4), libqt6dbus6 (>= 6.2.4), libqt6network6 (>= 6.2.4), libqt6svg6 (>= 6.2.4), libglibmm-2.4-1v5, libglib2.0-0, libpipewire-0.3-0 (>= 0.3.19-4)"
elif [[ "$flavor" == "pulse" ]]; then
   conflict="jamesdsp-pipewire"
   deps="libarchive13, qt6-qpa-plugins (>= 6.2.4), libqt6gui6 (>= 6.2.4), libqt6dbus6 (>= 6.2.4), libqt6widgets6  (>= 6.2.4), libqt6svgwidgets6 (>= 6.2.4), libqt6dbus6 (>= 6.2.4), libqt6network6 (>= 6.2.4), libqt6svg6 (>= 6.2.4), libglibmm-2.4-1v5, libglib2.0-0, libpulse-mainloop-glib0, libgstreamer1.0-0, gstreamer1.0-plugins-good"
else
  echo "ERROR: Unknown flavor"
  exit 1
fi

debname="jamesdsp-${flavor}_${version}_ubuntu22-04_amd64"
printf '%s\n' "$debname"
if [[ -e "$debname" ]]; then
  printf 'ERROR: Package staging path already exists: %s\n' "$debname" >&2
  exit 1
fi
if [[ -e "$debname.deb" ]]; then
  printf 'ERROR: Package output already exists: %s.deb\n' "$debname" >&2
  exit 1
fi
mkdir "$debname"
cleanup_staging()
{
  if [[ -d "$debname" ]]; then
    rm -rf "$debname"
  fi
}
trap cleanup_staging EXIT
mkdir "$debname/DEBIAN"
mkdir -p "$debname/usr/bin" "$debname/usr/share/applications" "$debname/usr/share/pixmaps"
cp "jamesdsp" "$debname/usr/bin/jamesdsp"

cp "resources/icons/icon.png" "$debname/usr/share/pixmaps/jamesdsp.png"
cp "LICENSE" "$debname/DEBIAN"

cat <<EOT > "$debname/usr/share/applications/jamesdsp.desktop"
[Desktop Entry]
Name=JamesDSP
GenericName=Audio effect processor
Comment=JamesDSP for Linux
Keywords=equalizer;audio;effect
Categories=AudioVideo;Audio
Exec=/usr/bin/jamesdsp
Icon=/usr/share/pixmaps/jamesdsp.png
StartupNotify=false
Terminal=false
Type=Application
EOT

cat <<EOT > "$debname/DEBIAN/control"
Package: jamesdsp-$flavor
Version: $version
Section: sound
Priority: optional
Architecture: amd64
Depends: $deps
Conflicts: $conflict
Maintainer: Tim Schneeberger (thepbone) <tim.schneeberger@outlook.de>
Description: JamesDSP for Linux
Homepage: https://github.com/Audio4Linux/JDSP4Linux
EOT

# dpkg-deb normalizes package metadata when run as root.  Keep the historical
# ownership request where permitted, but allow unprivileged reproducible local
# candidate builds to proceed with the caller's ownership.
if ! chown root:root "$debname/usr/share/applications/jamesdsp.desktop" 2>/dev/null; then
  if [[ "$(id -u)" -eq 0 ]]; then
    printf 'ERROR: Unable to set package metadata ownership\n' >&2
    exit 1
  fi
  printf 'warning: retaining local ownership for package metadata\n' >&2
fi
chmod 755 "$debname/usr/bin/jamesdsp"

dpkg-deb --build "$debname"
