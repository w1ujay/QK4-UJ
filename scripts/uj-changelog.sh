#!/usr/bin/env bash
# Prints the changelog of a fork release as JSON: {"added":[],"fixed":[],"changed":[]}
#
#   uj-changelog.sh <prev-tag> <tag> <upstream-ref>
#   uj-changelog.sh --previous-tag <tag>     prints the numeric uj.N tag before <tag>, or nothing for the first release
#
# Run from inside the repository. Only numeric uj.N tags are range boundaries: a <prev-tag> that is empty or is a
# rehearsal tag such as uj.test-1 is replaced by the numeric tag before <tag>. Commits the fork authored are listed one
# by one, and commits reachable from <upstream-ref> collapse to one line, because one merge can bring in hundreds.

set -euo pipefail

is_numeric_tag() {
    [[ "$1" =~ ^uj\.[0-9]+$ ]]
}

# The highest numeric tag below <tag>'s number, or the highest of all for a rehearsal tag, which has no number.
previous_tag() {
    local tag="$1" limit=""
    if is_numeric_tag "$tag"; then
        limit="${tag#uj.}"
    fi
    git tag --list 'uj.*' | { grep -E '^uj\.[0-9]+$' || true; } | sed 's/^uj\.//' | sort -n |
        awk -v limit="$limit" 'limit == "" || $1 + 0 < limit + 0 { last = $1 } END { if (last != "") print "uj." last }'
}

if [ "$#" -eq 2 ] && [ "$1" = "--previous-tag" ]; then
    previous_tag "$2"
    exit 0
fi

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <prev-tag> <tag> <upstream-ref>" >&2
    echo "       $0 --previous-tag <tag>" >&2
    exit 2
fi

prev="$1"
tag="$2"
upstream="$3"

if ! is_numeric_tag "$prev"; then
    prev="$(previous_tag "$tag")"
fi

# Without a previous tag the range is the whole history up to <tag>.
range=("$tag")
if [ -n "$prev" ]; then
    range=("$prev..$tag")
fi

# Subjects of the fork's own commits, oldest first. Merges carry no change of their own.
subjects="$(git log --reverse --no-merges --format=%s "${range[@]}" --not "$upstream")"
all_count="$(git rev-list --count --no-merges "${range[@]}")"
fork_count="$(git rev-list --count --no-merges "${range[@]}" --not "$upstream")"
upstream_count=$((all_count - fork_count))

# feat, fix, refactor and perf follow CLAUDE.md's changelog mapping. Everything else is left out.
jq -R -s --argjson upstream "$upstream_count" '
    def entries($types):
        [ split("\n")[]
          | capture("^(?<type>[a-z]+)(\\([^)]*\\))?!?: +(?<text>.+)$")?
          | select(.type as $type | $types | index($type))
          | .text
          | (.[0:1] | ascii_upcase) + .[1:] ];
    def upstream_line:
        if $upstream == 0 then []
        elif $upstream == 1 then ["Includes 1 upstream change"]
        else ["Includes \($upstream) upstream changes"]
        end;
    { added: entries(["feat"]),
      fixed: entries(["fix"]),
      changed: (entries(["refactor", "perf"]) + upstream_line) }
' <<<"$subjects"
