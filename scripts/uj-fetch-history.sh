#!/usr/bin/env bash
# Fetches the previous release's update.json into <history-dir>, for uj-update-json.sh.
#
#   uj-fetch-history.sh <prev-tag> <history-dir>
#
# update.json is the only store of the release history, so a failed lookup must stop the run. Starting afresh would
# publish a file that has lost every earlier release, and every later release would inherit the loss. Only two things
# start history here: no previous tag, and a previous release that gh reports, complete, without the file. A release
# from before release-uj.yml is the second case.
#
# gh takes the repository from GH_REPO and its credentials from GH_TOKEN.

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <prev-tag> <history-dir>" >&2
    exit 2
fi

prev="$1"
history_dir="$2"

if [ -z "$prev" ]; then
    echo "There is no previous release, so history starts here."
    exit 0
fi

# Captured first, so that set -e sees gh fail. As a pipeline into grep inside an if, a failure would read as "not there".
assets="$(gh release view "$prev" --json assets --jq '.assets[].name')"

if grep -qxF update.json <<<"$assets"; then
    gh release download "$prev" --pattern update.json --dir "$history_dir"
else
    echo "$prev has no update.json, so history starts here."
fi
