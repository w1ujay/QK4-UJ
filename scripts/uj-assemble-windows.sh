#!/usr/bin/env bash
# Builds the fork's Windows package.
#
#   uj-assemble-windows.sh <artifact-dir> <launcher-exe> <version> <out-zip>
#
# <artifact-dir> is the unpacked Windows build, with QK4.exe at its top. The zip holds:
#
#   QK4/QK4.exe                  the launcher
#   QK4/current.txt              names the version folder to run
#   QK4/versions/<version>/...   the app
#
# QK4 installs an update by unpacking only QK4/versions/<version>, so validateZipEntries in src/update/updatepolicy.cpp
# refuses anything else in the package. Keep the two in step.

set -euo pipefail

if [ "$#" -ne 4 ]; then
    echo "usage: $0 <artifact-dir> <launcher-exe> <version> <out-zip>" >&2
    exit 2
fi

artifact_dir="$1"
launcher="$2"
version="$3"
out_zip="$4"

if [[ ! "$version" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || [[ "$version" == *..* ]]; then
    echo "not a usable version folder name: $version" >&2
    exit 1
fi
if [ ! -f "$artifact_dir/QK4.exe" ]; then
    echo "the build has no QK4.exe: $artifact_dir" >&2
    exit 1
fi
if [ ! -f "$launcher" ]; then
    echo "missing launcher: $launcher" >&2
    exit 1
fi

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

mkdir -p "$stage/QK4/versions/$version"
cp "$launcher" "$stage/QK4/QK4.exe"
printf '%s\n' "$version" >"$stage/QK4/current.txt"
cp -R "$artifact_dir/." "$stage/QK4/versions/$version/"

# zip adds to a file that already exists, so a rerun must start clean.
out_dir="$(cd "$(dirname "$out_zip")" && pwd)"
out_path="$out_dir/$(basename "$out_zip")"
rm -f "$out_path"
(cd "$stage" && zip -qrX "$out_path" QK4)
