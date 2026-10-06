#!/usr/bin/env bash
# Screenshot harness: load the test city with the simulation paused, save the
# frame at every zoom (1-5) and rotation (0-3), quit. Git Bash on Windows.
#
#   scripts/run-shots.sh <build dir> <output dir> [env assignments...]
#
# Compare two output folders with tools/shotdiff.py. For a stable picture the
# run fixes the game's random seeds (determinism.fixed_seed) and turns off
# automata and clouds, which move in real time; extra assignments, such as
# OPENSC4_REPLACE=...,renderer.passthrough=ours, are passed on to
# run-autotest.sh (later ones win). See docs/renderer.md, "Screenshot harness".
set -euo pipefail

build="${1:?usage: run-shots.sh <build dir> <output dir> [VAR=value...]}"
out="${2:?usage: run-shots.sh <build dir> <output dir> [VAR=value...]}"
shift 2

repo="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$out"
if compgen -G "$out/*.png" >/dev/null; then
    echo "run-shots: $out already has PNGs; use an empty folder" >&2
    exit 1
fi

replace="determinism.fixed_seed=ours"
args=()
for assignment in "$@"; do
    case "$assignment" in
        OPENSC4_REPLACE=*) replace="$replace,${assignment#OPENSC4_REPLACE=}" ;;
        *) args+=("$assignment") ;;
    esac
done

"$repo/scripts/run-autotest.sh" "$build" \
    "OPENSC4_TEST_SHOTS=$(cygpath -w "$out")" \
    "OPENSC4_TEST_RENDER=RenderAutomata=0,RenderCloudsAndFog=0,RenderWaterEffects=0" \
    "OPENSC4_REPLACE=$replace" \
    "OPENSC4_TEST_TIMEOUT=300" \
    "${args[@]}"
