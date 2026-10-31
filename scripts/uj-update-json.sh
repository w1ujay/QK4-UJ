#!/usr/bin/env bash
# Prints the update.json of a fork release.
#
#   uj-update-json.sh <version> <uj> <date> <assets-dir> <base-url> <history-dir>
#
# <assets-dir> holds the three packages, named as the builders name them.
# <base-url> is the release's download URL, without a trailing slash.
# <history-dir> holds changelog.json, from uj-changelog.sh, and update.json from the previous release. A previous
# update.json may be absent, because the first release to have one starts its history there, but a corrupt one stops
# the run: it must not quietly cost every later release its history.

set -euo pipefail

HISTORY_LIMIT=20

if [ "$#" -ne 6 ]; then
    echo "usage: $0 <version> <uj> <date> <assets-dir> <base-url> <history-dir>" >&2
    exit 2
fi

version="$1"
uj="$2"
date="$3"
assets_dir="$4"
base_url="$5"
history_dir="$6"

if [[ ! "$uj" =~ ^[0-9]+$ ]]; then
    echo "uj must be a whole number: $uj" >&2
    exit 1
fi

sha256() {
    if command -v sha256sum >/dev/null; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

# Prints {key: {name, url, size, sha256}} for one package. The keys are the ones QK4 looks for.
asset_json() {
    local key="$1" name="$2" file="$assets_dir/$2"
    if [ ! -f "$file" ]; then
        echo "missing package: $file" >&2
        return 1
    fi
    jq -n --arg key "$key" --arg name "$name" --arg url "$base_url/$name" \
        --argjson size "$(wc -c <"$file" | tr -d ' ')" --arg sha256 "$(sha256 "$file")" \
        '{($key): {name: $name, url: $url, size: $size, sha256: $sha256}}'
}

windows="$(asset_json windows-x64 "QK4-$version-windows-x64.zip")"
flatpak="$(asset_json linux-flatpak-x86_64 "QK4-$version-linux-x86_64.flatpak")"
pi="$(asset_json raspberry-pi-arm64 "QK4-$version-raspberry-pi-arm64.tar.gz")"

previous='[]'
if [ -f "$history_dir/update.json" ]; then
    previous="$(jq -e '.history | arrays' "$history_dir/update.json")"
fi

jq -n \
    --arg version "$version" \
    --argjson uj "$uj" \
    --arg date "$date" \
    --argjson windows "$windows" \
    --argjson flatpak "$flatpak" \
    --argjson pi "$pi" \
    --slurpfile changelog "$history_dir/changelog.json" \
    --argjson previous "$previous" \
    --argjson limit "$HISTORY_LIMIT" \
    '
    ({uj: $uj, version: $version, date: $date} + ($changelog[0] | {added, fixed, changed})) as $entry
    | {
        schema: 1,
        version: $version,
        uj: $uj,
        date: $date,
        assets: ($windows + $flatpak + $pi),
        history: (([$entry] + ($previous | map(select(.uj != $uj))))[0:$limit])
      }
    '
