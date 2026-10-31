#!/usr/bin/env bash
# Tests the fork release scripts against temporary git repositories and real zip files.
#
# Needs git, jq and zip. The zip round trip also needs bsdtar, the same libarchive that is Windows tar.exe, and is
# skipped with a notice without it, unless UJ_REQUIRE_BSDTAR=1 says skipping is a failure. CI sets that.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS="$SCRIPT_DIR/../../scripts"
CHANGELOG="$SCRIPTS/uj-changelog.sh"
UPDATE_JSON="$SCRIPTS/uj-update-json.sh"
ASSEMBLE="$SCRIPTS/uj-assemble-windows.sh"
FETCH_HISTORY="$SCRIPTS/uj-fetch-history.sh"

VERSION="0.8.0-uj.14"
BASE_URL="https://github.com/w1ujay/QK4-UJ/releases/download/uj.14"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
FAILED=0
CURRENT=""

fail() {
    echo "  $CURRENT: $*"
    FAILED=$((FAILED + 1))
}

expect_json() { # <file> <jq expression> <description>
    jq -e "$2" "$1" >/dev/null 2>&1 || fail "$3 (got: $(tr -d '\n' <"$1" | cut -c1-400))"
}

expect_eq() { # <expected> <actual> <description>
    [ "$1" = "$2" ] || fail "$3: expected [$1], got [$2]"
}

run_test() {
    CURRENT="$1"
    local before=$FAILED
    "$1"
    if [ "$FAILED" -eq "$before" ]; then
        echo "PASS   $1"
    else
        echo "FAIL   $1"
    fi
}

# --- repositories -----------------------------------------------------------------------------------------------------

git_q() {
    GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null git -c user.name=t -c user.email=t@example.com \
        -c commit.gpgsign=false -c tag.gpgsign=false "$@" >/dev/null
}

new_repo() {
    REPO="$WORK/repo-$CURRENT"
    git_q init -q -b main "$REPO"
    commit "feat: the very first commit"
    git_q -C "$REPO" tag uj.12
}

commit() {
    git_q -C "$REPO" commit -q --allow-empty -m "$1"
}

tag() {
    git_q -C "$REPO" tag "$1"
}

# Runs from inside the repository, as the workflow does.
changelog() { # <prev> <tag> <upstream ref> <out>
    (cd "$REPO" && bash "$CHANGELOG" "$1" "$2" "$3") >"$4" 2>"$4.err" || fail "uj-changelog.sh failed: $(cat "$4.err")"
}

# --- changelog --------------------------------------------------------------------------------------------------------

forkCommitsListed() {
    new_repo
    git_q -C "$REPO" branch upstream
    commit "feat: add the thing"
    commit "feat(audio): add a jitter buffer"
    commit "fix: stop the crash"
    commit "fix(ui)!: repair the popup"
    commit "refactor: tidy the loop"
    commit "perf(dsp): draw faster"
    tag uj.13
    local out="$WORK/$CURRENT.json"
    changelog uj.12 uj.13 upstream "$out"
    expect_json "$out" '.added == ["Add the thing", "Add a jitter buffer"]' "feat maps to added, oldest first"
    expect_json "$out" '.fixed == ["Stop the crash", "Repair the popup"]' "fix maps to fixed"
    expect_json "$out" '.changed == ["Tidy the loop", "Draw faster"]' "refactor and perf map to changed"
}

skippedTypesLeftOut() {
    new_repo
    git_q -C "$REPO" branch upstream
    local type
    for type in docs chore ci test style; do
        commit "$type: left out"
        commit "$type(scope): left out"
    done
    commit "not a conventional commit"
    commit "feature: looks close but is not a type"
    commit "feat: kept"
    tag uj.13
    local out="$WORK/$CURRENT.json"
    changelog uj.12 uj.13 upstream "$out"
    expect_json "$out" '. == {"added":["Kept"],"fixed":[],"changed":[]}' "only feat survives"
}

specialCharactersSurvive() {
    new_repo
    git_q -C "$REPO" branch upstream
    commit 'fix: handle "quotes", a \backslash and $HOME'
    tag uj.13
    local out="$WORK/$CURRENT.json"
    changelog uj.12 uj.13 upstream "$out"
    expect_json "$out" '.fixed == ["Handle \"quotes\", a \\backslash and $HOME"]' "subject is carried verbatim"
}

upstreamCollapsed() {
    new_repo
    git_q -C "$REPO" branch fork-side
    # Three commits on upstream's side. Their types must not put them in the list.
    git_q -C "$REPO" checkout -q -b upstream main
    commit "feat: upstream one"
    commit "fix: upstream two"
    commit "docs: upstream three"
    git_q -C "$REPO" checkout -q fork-side
    commit "feat: fork work"
    git_q -C "$REPO" merge -q --no-ff -m "Merge upstream development" upstream
    tag uj.13
    local out="$WORK/$CURRENT.json"
    changelog uj.12 uj.13 upstream "$out"
    expect_json "$out" '.added == ["Fork work"]' "only the fork commit is listed"
    expect_json "$out" '.fixed == []' "upstream fix is not listed"
    expect_json "$out" '.changed == ["Includes 3 upstream changes"]' "upstream commits collapse to one line"
}

oneUpstreamChangeIsSingular() {
    new_repo
    git_q -C "$REPO" branch upstream
    git_q -C "$REPO" checkout -q upstream
    commit "feat: upstream one"
    git_q -C "$REPO" checkout -q main
    git_q -C "$REPO" merge -q --no-ff -m "Merge upstream development" upstream
    tag uj.13
    local out="$WORK/$CURRENT.json"
    changelog uj.12 uj.13 upstream "$out"
    expect_json "$out" '.changed == ["Includes 1 upstream change"]' "singular wording"
}

numericTagsOnly() {
    new_repo
    git_q -C "$REPO" branch upstream
    commit "feat: before the rehearsal"
    tag uj.test-1
    commit "feat: after the rehearsal"
    tag uj.13
    local out="$WORK/$CURRENT.json"

    changelog uj.test-1 uj.13 upstream "$out"
    expect_json "$out" '.added == ["Before the rehearsal", "After the rehearsal"]' \
        "a rehearsal tag is not a boundary: uj.12 is"

    changelog "" uj.13 upstream "$out"
    expect_json "$out" '.added == ["Before the rehearsal", "After the rehearsal"]' "an empty boundary finds uj.12"

    expect_eq "uj.12" "$(cd "$REPO" && bash "$CHANGELOG" --previous-tag uj.13 2>&1)" "previous tag of uj.13"
    expect_eq "uj.13" "$(cd "$REPO" && bash "$CHANGELOG" --previous-tag uj.test-1 2>&1)" "previous tag of a rehearsal"
    expect_eq "uj.13" "$(cd "$REPO" && bash "$CHANGELOG" --previous-tag uj.20 2>&1)" "previous tag of a later number"
}

previousTagIsNumericNotAlphabetical() {
    new_repo
    tag uj.9
    tag uj.100
    expect_eq "uj.12" "$(cd "$REPO" && bash "$CHANGELOG" --previous-tag uj.13 2>&1)" "12 sorts above 9"
    expect_eq "uj.100" "$(cd "$REPO" && bash "$CHANGELOG" --previous-tag uj.test-1 2>&1)" "100 sorts above 12"
}

rangeCoversBranchThatMissesThePreviousTag() {
    # The rehearsal's case: the previous release's tag is on main, and the tagged branch left before it.
    new_repo
    git_q -C "$REPO" branch upstream
    git_q -C "$REPO" checkout -q -b feature main
    commit "feat: branch work"
    tag uj.test-1
    git_q -C "$REPO" checkout -q main
    commit "feat: only on main"
    tag uj.13
    local out="$WORK/$CURRENT.json"
    changelog uj.13 uj.test-1 upstream "$out"
    expect_json "$out" '.added == ["Branch work"]' "lists the branch and not main"
}

noPreviousTag() {
    REPO="$WORK/repo-$CURRENT"
    git_q init -q -b main "$REPO"
    commit "feat: the very first commit"
    git_q -C "$REPO" branch upstream
    commit "fix: later"
    tag uj.13
    expect_eq "" "$(cd "$REPO" && bash "$CHANGELOG" --previous-tag uj.13 2>&1)" "nothing is before the first tag"
    local out="$WORK/$CURRENT.json"
    changelog "" uj.13 upstream "$out"
    expect_json "$out" '.fixed == ["Later"]' "the whole history is the range"
}

# --- update.json ------------------------------------------------------------------------------------------------------

make_assets() { # <dir>
    mkdir -p "$1"
    printf 'windows package' >"$1/QK4-$VERSION-windows-x64.zip"
    printf 'flatpak' >"$1/QK4-$VERSION-linux-x86_64.flatpak"
    printf 'pi package, longest' >"$1/QK4-$VERSION-raspberry-pi-arm64.tar.gz"
    # Beside the packages in the workflow. It must not become an asset.
    printf 'noise' >"$1/SHA256SUMS.txt"
}

make_history_dir() { # <dir>
    mkdir -p "$1"
    echo '{"added":["New thing"],"fixed":["Old bug"],"changed":[]}' >"$1/changelog.json"
}

file_sha256() {
    if command -v sha256sum >/dev/null; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

update_json() { # <assets> <history> <out>
    bash "$UPDATE_JSON" "$VERSION" 14 2026-11-02 "$1" "$BASE_URL" "$2" >"$3" 2>"$3.err" ||
        fail "uj-update-json.sh failed: $(cat "$3.err")"
}

check_asset() { # <file> <key> <asset file>
    local name
    name="$(basename "$3")"
    expect_json "$1" ".assets[\"$2\"].name == \"$name\"" "$2 name"
    expect_json "$1" ".assets[\"$2\"].url == \"$BASE_URL/$name\"" "$2 url"
    expect_json "$1" ".assets[\"$2\"].size == $(wc -c <"$3" | tr -d ' ')" "$2 size"
    expect_json "$1" ".assets[\"$2\"].sha256 == \"$(file_sha256 "$3")\"" "$2 sha256"
}

updateJsonValid() {
    local assets="$WORK/$CURRENT-assets" history="$WORK/$CURRENT-history" out="$WORK/$CURRENT.json"
    make_assets "$assets"
    make_history_dir "$history"
    update_json "$assets" "$history" "$out"
    expect_json "$out" '.schema == 1' "schema"
    expect_json "$out" ".version == \"$VERSION\"" "version"
    expect_json "$out" '.uj == 14' "uj is a number"
    expect_json "$out" '.date == "2026-11-02"' "date"
    expect_json "$out" '.assets | keys == ["linux-flatpak-x86_64", "raspberry-pi-arm64", "windows-x64"]' "asset keys"
    check_asset "$out" windows-x64 "$assets/QK4-$VERSION-windows-x64.zip"
    check_asset "$out" linux-flatpak-x86_64 "$assets/QK4-$VERSION-linux-x86_64.flatpak"
    check_asset "$out" raspberry-pi-arm64 "$assets/QK4-$VERSION-raspberry-pi-arm64.tar.gz"
    expect_json "$out" \
        ".history == [{\"uj\":14,\"version\":\"$VERSION\",\"date\":\"2026-11-02\",\"added\":[\"New thing\"],\"fixed\":[\"Old bug\"],\"changed\":[]}]" \
        "a first release's history is itself"
}

historyPrepended() {
    local assets="$WORK/$CURRENT-assets" history="$WORK/$CURRENT-history" out="$WORK/$CURRENT.json"
    make_assets "$assets"
    make_history_dir "$history"
    jq -n '{schema:1,history:[{uj:13,version:"0.8.0-uj.13",date:"2026-11-01",added:["Older"],fixed:[],changed:[]}]}' \
        >"$history/update.json"
    update_json "$assets" "$history" "$out"
    expect_json "$out" '[.history[].uj] == [14, 13]' "newest first"
    expect_json "$out" '.history[1].added == ["Older"]' "the older entry is carried unchanged"
}

historyCappedAt20() {
    local assets="$WORK/$CURRENT-assets" history="$WORK/$CURRENT-history" out="$WORK/$CURRENT.json"
    make_assets "$assets"
    make_history_dir "$history"
    jq -n '{schema:1,history:[range(13;-11;-1) | {uj:.,version:"x",date:"d",added:[],fixed:[],changed:[]}]}' \
        >"$history/update.json"
    update_json "$assets" "$history" "$out"
    expect_json "$out" '.history | length == 20' "20 entries"
    expect_json "$out" '.history[0].uj == 14 and .history[1].uj == 13 and .history[19].uj == -5' "newest 20 kept"
}

retaggedNumberIsNotListedTwice() {
    local assets="$WORK/$CURRENT-assets" history="$WORK/$CURRENT-history" out="$WORK/$CURRENT.json"
    make_assets "$assets"
    make_history_dir "$history"
    jq -n '{schema:1,history:[{uj:14,version:"old",date:"d",added:[],fixed:[],changed:[]}]}' >"$history/update.json"
    update_json "$assets" "$history" "$out"
    expect_json "$out" '[.history[].uj] == [14] and .history[0].version != "old"' "the new entry replaces it"
}

missingAssetFails() {
    local assets="$WORK/$CURRENT-assets" history="$WORK/$CURRENT-history"
    make_assets "$assets"
    make_history_dir "$history"
    rm "$assets/QK4-$VERSION-linux-x86_64.flatpak"
    if bash "$UPDATE_JSON" "$VERSION" 14 2026-11-02 "$assets" "$BASE_URL" "$history" >/dev/null 2>&1; then
        fail "a missing package must not produce an update.json"
    fi
}

corruptHistoryFails() {
    local assets="$WORK/$CURRENT-assets" history="$WORK/$CURRENT-history"
    make_assets "$assets"
    make_history_dir "$history"
    echo 'not json' >"$history/update.json"
    if bash "$UPDATE_JSON" "$VERSION" 14 2026-11-02 "$assets" "$BASE_URL" "$history" >/dev/null 2>&1; then
        fail "a corrupt previous file must stop the release, not silently lose history"
    fi
}

# --- previous update.json ---------------------------------------------------------------------------------------------

# A stand-in for gh, first on PATH. FAKE_VIEW_STATUS and FAKE_DOWNLOAD_STATUS set its exit codes, and FAKE_ASSETS is what
# `release view` prints. Every call is logged, and `release download` writes the file it was asked for.
make_fake_gh() { # <dir>
    mkdir -p "$1"
    cat >"$1/gh" <<'FAKE'
#!/usr/bin/env bash
echo "$*" >>"$FAKE_GH_LOG"
case "$1 $2" in
"release view")
    printf '%b' "${FAKE_ASSETS:-}"
    exit "${FAKE_VIEW_STATUS:-0}"
    ;;
"release download")
    [ "${FAKE_DOWNLOAD_STATUS:-0}" -eq 0 ] || exit "$FAKE_DOWNLOAD_STATUS"
    while [ "$#" -gt 0 ]; do
        if [ "$1" = "--dir" ]; then
            echo '{"history":[]}' >"$2/update.json"
        fi
        shift
    done
    ;;
*)
    exit 64
    ;;
esac
FAKE
    chmod +x "$1/gh"
}

fetch_history() { # <prev tag> <history dir>, with the FAKE_* settings in the environment
    make_fake_gh "$WORK/$CURRENT-bin"
    mkdir -p "$2"
    FAKE_GH_LOG="$WORK/$CURRENT-gh.log" PATH="$WORK/$CURRENT-bin:$PATH" bash "$FETCH_HISTORY" "$1" "$2" \
        >"$WORK/$CURRENT.out" 2>&1
}

historyFetchedWhenPresent() {
    local history="$WORK/$CURRENT-history"
    FAKE_ASSETS='SHA256SUMS.txt\nupdate.json\nQK4.zip\n' fetch_history uj.14 "$history" ||
        fail "should succeed: $(cat "$WORK/$CURRENT.out")"
    [ -f "$history/update.json" ] || fail "update.json was not fetched"
    grep -q 'release download uj.14 --pattern update.json' "$WORK/$CURRENT-gh.log" || fail "wrong download call"
}

historyFetchedFromALongListing() {
    # grep -q stops reading at its match. Under pipefail that used to read as "not found" for a gh still writing.
    local history="$WORK/$CURRENT-history" assets
    assets="$(printf 'update.json\\n'; printf 'padding-file-name.zip\\n%.0s' $(seq 1 20000))"
    FAKE_ASSETS="$assets" fetch_history uj.14 "$history" || fail "should succeed: $(cat "$WORK/$CURRENT.out")"
    [ -f "$history/update.json" ] || fail "update.json was not fetched"
}

historyStartsFreshWithoutTheFile() {
    local history="$WORK/$CURRENT-history"
    FAKE_ASSETS='SHA256SUMS.txt\nnot-update.json\nupdate.json.bak\n' fetch_history uj.13 "$history" ||
        fail "a release without the file is not an error: $(cat "$WORK/$CURRENT.out")"
    [ ! -e "$history/update.json" ] || fail "nothing should be fetched"
    if grep -q 'release download' "$WORK/$CURRENT-gh.log"; then
        fail "no download should be attempted"
    fi
    grep -q 'uj.13 has no update.json' "$WORK/$CURRENT.out" || fail "the fresh start must be stated"
}

historyStartsFreshWithNoPreviousRelease() {
    local history="$WORK/$CURRENT-history"
    fetch_history "" "$history" || fail "the first release is not an error: $(cat "$WORK/$CURRENT.out")"
    [ ! -e "$WORK/$CURRENT-gh.log" ] || fail "gh has nothing to look up"
}

ghViewFailureStops() {
    local history="$WORK/$CURRENT-history"
    if FAKE_VIEW_STATUS=1 fetch_history uj.14 "$history"; then
        fail "a failed lookup must stop the run, not start history again"
    fi
    if grep -q 'has no update.json' "$WORK/$CURRENT.out"; then
        fail "a failed lookup must not be reported as a fresh start"
    fi
}

ghViewFailureAfterPartialOutputStops() {
    local history="$WORK/$CURRENT-history"
    if FAKE_ASSETS='update.json\n' FAKE_VIEW_STATUS=1 fetch_history uj.14 "$history"; then
        fail "a failed lookup must stop the run whatever it printed first"
    fi
    if grep -q 'release download' "$WORK/$CURRENT-gh.log"; then
        fail "nothing should be downloaded after a failed lookup"
    fi
}

ghDownloadFailureStops() {
    local history="$WORK/$CURRENT-history"
    if FAKE_ASSETS='update.json\n' FAKE_DOWNLOAD_STATUS=1 fetch_history uj.14 "$history"; then
        fail "a failed download must stop the run"
    fi
}

# --- Windows package --------------------------------------------------------------------------------------------------

make_artifact() { # <dir>
    mkdir -p "$1/tls" "$1/plugins/platforms/deep"
    printf 'the app' >"$1/QK4.exe"
    printf 'a dll' >"$1/Qt6Core.dll"
    printf 'tls' >"$1/tls/qopensslbackend.dll"
    printf 'nested' >"$1/plugins/platforms/deep/qwindows.dll"
}

assemble() { # <zip>
    local artifact="$WORK/$CURRENT-artifact" launcher="$WORK/$CURRENT-launcher.exe"
    make_artifact "$artifact"
    printf 'the launcher' >"$launcher"
    bash "$ASSEMBLE" "$artifact" "$launcher" "$VERSION" "$1" >"$1.log" 2>&1 ||
        fail "uj-assemble-windows.sh failed: $(cat "$1.log")"
}

zip_entries() { # <zip>
    zip -sf "$1" | sed -n 's/^  //p'
}

zipLayout() {
    local package="$WORK/$CURRENT.zip" entries
    assemble "$package"
    entries="$(zip_entries "$package")"
    local required
    for required in QK4/QK4.exe QK4/current.txt "QK4/versions/$VERSION/QK4.exe" \
        "QK4/versions/$VERSION/tls/qopensslbackend.dll" "QK4/versions/$VERSION/plugins/platforms/deep/qwindows.dll"; do
        echo "$entries" | grep -qxF "$required" || fail "missing $required"
    done
    local stray
    stray="$(echo "$entries" | grep -vxE 'QK4/|QK4/QK4\.exe|QK4/current\.txt|QK4/versions/' |
        grep -vF "QK4/versions/$VERSION/")"
    expect_eq "" "$stray" "nothing else at the top level"
}

zipReplacesAnOldFile() {
    local package="$WORK/$CURRENT.zip"
    assemble "$package"
    mkdir -p "$WORK/$CURRENT-old/stale"
    (cd "$WORK/$CURRENT-old" && touch stale/file && zip -qr "$package" stale)
    assemble "$package"
    if zip_entries "$package" | grep -q stale; then
        fail "an earlier zip at the same path must not be added to"
    fi
}

assembleRefusesAnIncompleteArtifact() {
    local artifact="$WORK/$CURRENT-artifact" launcher="$WORK/$CURRENT-launcher.exe"
    make_artifact "$artifact"
    rm "$artifact/QK4.exe"
    printf 'the launcher' >"$launcher"
    if bash "$ASSEMBLE" "$artifact" "$launcher" "$VERSION" "$WORK/$CURRENT.zip" >/dev/null 2>&1; then
        fail "a package without the app must not be built"
    fi
    make_artifact "$artifact"
    rm "$launcher"
    if bash "$ASSEMBLE" "$artifact" "$launcher" "$VERSION" "$WORK/$CURRENT.zip" >/dev/null 2>&1; then
        fail "a package without the launcher must not be built"
    fi
}

assembleRefusesAPathInTheVersion() {
    local artifact="$WORK/$CURRENT-artifact" launcher="$WORK/$CURRENT-launcher.exe"
    make_artifact "$artifact"
    printf 'the launcher' >"$launcher"
    if bash "$ASSEMBLE" "$artifact" "$launcher" "../evil" "$WORK/$CURRENT.zip" >/dev/null 2>&1; then
        fail "a version with a path in it must be refused"
    fi
}

zipRoundTrip() {
    if ! command -v bsdtar >/dev/null; then
        if [ "${UJ_REQUIRE_BSDTAR:-0}" = "1" ]; then
            fail "bsdtar is required here"
        else
            echo "SKIP   zipRoundTrip: bsdtar is not installed"
        fi
        return
    fi

    # The layout QK4 builds: the zip sits beside the incoming folder and tar runs inside it, using relative paths only.
    local versions="$WORK/$CURRENT-versions" id="abc123" package
    mkdir -p "$versions/.incoming-$id"
    package="$versions/.package-$id.zip"
    assemble "$package"
    local incoming="$versions/.incoming-$id"
    (
        cd "$incoming" || exit 1
        bsdtar -tf "../.package-$id.zip" >"$WORK/$CURRENT.list" &&
            bsdtar -xf "../.package-$id.zip" --strip-components=3 "QK4/versions/$VERSION"
    ) || fail "bsdtar failed"

    expect_eq "the app" "$(cat "$incoming/QK4.exe" 2>/dev/null)" "the app lands directly in the incoming folder"
    expect_eq "tls" "$(cat "$incoming/tls/qopensslbackend.dll" 2>/dev/null)" "subfolder intact"
    expect_eq "nested" "$(cat "$incoming/plugins/platforms/deep/qwindows.dll" 2>/dev/null)" "deep subfolder intact"
    [ ! -e "$incoming/QK4" ] || fail "leading folders were not stripped"
    [ ! -e "$incoming/current.txt" ] || fail "only the version folder may be extracted"

    expect_eq "the launcher" "$(bsdtar -xOf "$package" QK4/QK4.exe)" "the launcher is at the root"
    expect_eq "$VERSION" "$(bsdtar -xOf "$package" QK4/current.txt)" "current.txt names the version"
}

# ----------------------------------------------------------------------------------------------------------------------

run_test forkCommitsListed
run_test skippedTypesLeftOut
run_test specialCharactersSurvive
run_test upstreamCollapsed
run_test oneUpstreamChangeIsSingular
run_test numericTagsOnly
run_test previousTagIsNumericNotAlphabetical
run_test rangeCoversBranchThatMissesThePreviousTag
run_test noPreviousTag
run_test updateJsonValid
run_test historyPrepended
run_test historyCappedAt20
run_test retaggedNumberIsNotListedTwice
run_test missingAssetFails
run_test corruptHistoryFails
run_test historyFetchedWhenPresent
run_test historyFetchedFromALongListing
run_test historyStartsFreshWithoutTheFile
run_test historyStartsFreshWithNoPreviousRelease
run_test ghViewFailureStops
run_test ghViewFailureAfterPartialOutputStops
run_test ghDownloadFailureStops
run_test zipLayout
run_test zipReplacesAnOldFile
run_test assembleRefusesAnIncompleteArtifact
run_test assembleRefusesAPathInTheVersion
run_test zipRoundTrip

if [ "$FAILED" -ne 0 ]; then
    echo "$FAILED check(s) failed"
    exit 1
fi
echo "All checks passed"
