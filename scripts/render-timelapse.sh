#!/usr/bin/env bash
# Timelapse from the copies game.timelapse keeps (docs/timelapse.md): loads
# each copy in turn, unattended and with the simulation paused, and saves one
# frame from the same view; with ffmpeg on the PATH the frames also become
# timelapse.mp4. Git Bash on Windows.
#
#   scripts/render-timelapse.sh <build dir> <copies folder> <output dir> [VAR=value...]
#
# <copies folder> is <user folder>\Timelapse\<region>\<city file name>; the
# region comes from <user folder>\Regions\<region> (OPENSC4_TIMELAPSE_USERDIR
# names another user folder). Each copy is loaded in a disposable user folder
# (%LOCALAPPDATA%\opensc4\timelapse-env, OPENSC4_TIMELAPSE_ENV), never in the
# player's own. OPENSC4_TIMELAPSE_VIEW=<zoom>:<rotation> (default 0:0, zoom
# 0-5 with 0 farthest, rotation 0-3), centred on the middle of the city. Frames already
# in <output dir> are kept, so an interrupted run can be resumed.
# OPENSC4_TIMELAPSE_REPLACE adds replacements (e.g. harness.multi_instance=ours
# with OPENSC4_PARALLEL=1, to render while other copies of the game run).
set -euo pipefail

build="${1:?usage: render-timelapse.sh <build dir> <copies folder> <output dir> [VAR=value...]}"
copies="${2:?usage: render-timelapse.sh <build dir> <copies folder> <output dir> [VAR=value...]}"
out="${3:?usage: render-timelapse.sh <build dir> <copies folder> <output dir> [VAR=value...]}"
shift 3
for assignment in "$@"; do
    export "${assignment?}"
done

repo="$(cd "$(dirname "$0")/.." && pwd)"
copies="$(cd "$copies" && pwd)"
city_file="$(basename "$copies")"
region="$(basename "$(dirname "$copies")")"
userdir="${OPENSC4_TIMELAPSE_USERDIR:-$(cd "$copies/../../.." && pwd)}"
env="${OPENSC4_TIMELAPSE_ENV:-$(cygpath "$LOCALAPPDATA")/opensc4/timelapse-env}"
view="${OPENSC4_TIMELAPSE_VIEW:-0:0}"
[ -d "$userdir/Regions/$region" ] || { echo "render-timelapse: no $userdir/Regions/$region" >&2; exit 1; }
mkdir -p "$out"

pause() { timeout "$1" bash -c 'while :; do sleep 1; done' || true; }

n=0
for copy in "$copies"/*.sc4; do
    date="$(basename "$copy" .sc4)"
    [ -f "$out/$date.png" ] && continue
    shots="$env/shots"
    for try in 1 2 3; do
        rm -rf "$env/Regions" "$shots"
        mkdir -p "$env/Plugins" "$env/Regions" "$shots"
        cp -r "$userdir/Regions/$region" "$env/Regions/"
        cp "$userdir/SimCity 4.cfg" "$env/" 2>/dev/null || true
        cp "$copy" "$env/Regions/$region/$city_file.sc4"
        "$repo/scripts/run-autotest.sh" "$build" "OPENSC4_TESTENV=$env" "OPENSC4_TEST_REGION=$region" \
            "OPENSC4_TEST_CITY=$city_file.sc4" "OPENSC4_TEST_SHOTS=$(cygpath -w "$shots")" \
            "OPENSC4_TEST_SHOT_VIEWS=$view" OPENSC4_TEST_SHOT_CENTER=1 OPENSC4_TEST_SHOT_HIDE_UI=1 "OPENSC4_TEST_RENDER=RenderAutomata=0,RenderCloudsAndFog=0" \
            "OPENSC4_REPLACE=determinism.fixed_seed=ours${OPENSC4_TIMELAPSE_REPLACE:+,$OPENSC4_TIMELAPSE_REPLACE}" \
            "OPENSC4_TEST_TIMEOUT=300" >"$env/run.out" 2>&1 || true
        frame="$(ls "$shots"/*.png 2>/dev/null | head -1)"
        if [ -n "$frame" ]; then
            mv "$frame" "$out/$date.png"
            n=$((n + 1))
            echo "render-timelapse: $date"
            break
        fi
        echo "render-timelapse: no frame for $date (try $try); last output:" >&2
        tail -3 "$env/run.out" >&2
        pause 30
    done
    pause 20 # Steam relaunches the game when it starts right after the last run
done
echo "render-timelapse: $n new frames in $out"

if command -v ffmpeg >/dev/null; then
    list="$out/frames.txt"
    : >"$list"
    for frame in "$out"/*.png; do
        echo "file '$(basename "$frame")'" >>"$list"
        echo "duration 0.25" >>"$list"
    done
    (cd "$out" && ffmpeg -loglevel error -y -f concat -safe 0 -i frames.txt -vf "fps=8,format=yuv420p" timelapse.mp4) &&
        echo "render-timelapse: $out/timelapse.mp4"
fi
