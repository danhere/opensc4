#!/usr/bin/env bash
# Screenshot harness over every test city: runs scripts/run-shots.sh once per
# city into <output dir>/<city tag>, then, with --compare <baseline dir>,
# compares each against <baseline dir>/<city tag> with tools/shotdiff.py.
# Git Bash on Windows.
#
#   scripts/run-shots-all.sh <build dir> <output dir> [--compare <baseline dir>] [env assignments...]
#
# Example: stock baselines, then the D3D11 driver against them:
#   scripts/run-shots-all.sh "$B" "$LOCALAPPDATA/opensc4/shots/stock"
#   scripts/run-shots-all.sh "$B" "$LOCALAPPDATA/opensc4/shots/ours" --compare "$LOCALAPPDATA/opensc4/shots/stock" \
#       OPENSC4_REPLACE=renderer.d3d11=ours
set -euo pipefail

build="${1:?usage: run-shots-all.sh <build dir> <output dir> [--compare <baseline dir>] [VAR=value...]}"
out="${2:?usage: run-shots-all.sh <build dir> <output dir> [--compare <baseline dir>] [VAR=value...]}"
shift 2
baseline=""
if [ "${1:-}" = "--compare" ]; then
    baseline="${2:?--compare needs a baseline dir}"
    shift 2
fi

repo="$(cd "$(dirname "$0")/.." && pwd)"
# POSIX paths, so globs work on paths given as C:\... (e.g. from $LOCALAPPDATA).
out="$(cygpath -u "$out")"
[ -n "$baseline" ] && baseline="$(cygpath -u "$baseline")"
# Tutorial cities in the test region: a small town, a dense downtown, highways.
cities=("Making Money Tutorial:makingmoney" "Big City Tutorial:bigcity" "Rush Hour Tutorial:rushhour")

pause() { timeout "$1" bash -c 'while :; do sleep 1; done' || true; }

status=0
for city in "${cities[@]}"; do
    name="${city%%:*}"
    tag="${city##*:}"
    dir="$out/$tag"
    mkdir -p "$dir"
    # Steam's DRM stub sometimes relaunches the game when it starts soon after
    # the previous run; run-shots.sh then produces nothing. Wait and retry.
    for try in 1 2 3; do
        "$repo/scripts/run-shots.sh" "$build" "$dir" "OPENSC4_TEST_CITY=$name" "$@" >"$dir.log" 2>&1 || true
        compgen -G "$dir/*.png" >/dev/null && break
        echo "run-shots-all: $tag produced no shots (try $try), retrying" >&2
        pause 30
    done
    echo "$tag: $(compgen -G "$dir/*.png" | wc -l) shots (log: $dir.log)"
    if [ -n "$baseline" ]; then
        python3 "$repo/tools/shotdiff.py" "$(cygpath -w "$baseline/$tag")" "$(cygpath -w "$dir")" \
            --tolerance 32 --max-differing 1.2 --diff-dir "$(cygpath -w "$dir-diff")" | tail -1 || status=1
    fi
    pause 15
done
exit "$status"
