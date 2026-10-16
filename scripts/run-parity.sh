#!/usr/bin/env bash
# P5 parity harness: from every save in <saves dir>, run the city for <days>
# game days twice, once with replacement set A and once with set B, and compare
# the two runs exactly: daily fingerprints (tools/re/fpdiff.py) and the saved
# records whose layout is known (tools/re/sc4save.py --compare). Both runs use
# fixed seeds, the virtual clock and fixed ticks, under which two runs of the
# original agree exactly (docs/experiments/determinism.md, third round). In
# case they do not, a pair that differs is run again, up to
# OPENSC4_PARITY_RETRIES more times (default 3), and the verdict comes from
# all runs of the save together (tools/re/paritygroups.py): it passes when
# every outcome of B is also an outcome of A. Git Bash on Windows.
#
#   scripts/run-parity.sh <build dir> <output dir> <days> <saves dir> [env assignments...]
#
# <saves dir> holds one folder per city, named as the city ("Making Money
# Tutorial"), with any number of saves of that city in it (*.sc4).
# OPENSC4_PARITY_A and OPENSC4_PARITY_B are the replacement lists of the two
# runs (OPENSC4_REPLACE syntax; A defaults to none, the original game). Other
# assignments apply to both runs. Runs whose results already exist in
# <output dir> are reused, so an interrupted run can be resumed.
# OPENSC4_PARITY_JOBS=<n> runs n saves at a time, each in its own copy of the
# game and its own user folder (default 1). A copy needs up to 2 GB.
# OPENSC4_PARITY_ENV=<folder> puts the jobs' user folders there (default
# %LOCALAPPDATA%\opensc4\p4env), so batches run at the same time keep apart.
# Runs use OPENSC4_TEST_QUICK=1 (src/core/autotest.h: short region wait, fast
# save) unless given OPENSC4_TEST_QUICK=0. OPENSC4_PARITY_PAUSE=<s> waits
# before each game start (default 20 with one job, against Steam relaunches;
# 0 with several, where none were seen).
#
# Example: the pass-through pollution simulator against the original:
#   scripts/run-parity.sh "$B" "$LOCALAPPDATA/opensc4/parity/proxy" 7 "$LOCALAPPDATA/opensc4/parity/saves" \
#       OPENSC4_PARITY_B=simulators.pollution.proxy=ours
set -euo pipefail

usage="usage: run-parity.sh <build dir> <output dir> <days> <saves dir> [VAR=value...]"
build="${1:?$usage}"
out="$(cygpath -u "${2:?$usage}")"
days="${3:?$usage}"
saves="$(cygpath -u "${4:?$usage}")"
shift 4
for assignment in "$@"; do
    export "${assignment?}"
done
retries="${OPENSC4_PARITY_RETRIES:-3}"
jobs="${OPENSC4_PARITY_JOBS:-1}"
start_pause="${OPENSC4_PARITY_PAUSE:-$([ "$jobs" = 1 ] && echo 20 || echo 0)}"
export OPENSC4_TEST_QUICK="${OPENSC4_TEST_QUICK:-1}"

repo="$(cd "$(dirname "$0")/.." && pwd)"
base="determinism.fixed_seed=ours,determinism.virtual_clock=ours,determinism.fixed_ticks=ours,determinism.agent_order=ours,determinism.zero_alloc=ours,determinism.sim_timers=ours,determinism.lights_at_once=ours,determinism.dispatch_info=ours,resources.stale_record_fix=ours"
# Improvements on by default that change the game by themselves are turned
# off, so both runs play classic SC4 (game.date_sync: docs/date-sync.md;
# fixes.travel_jobs: docs/citylot.md).
base="$base,game.date_sync=original,fixes.travel_jobs=original"
# Parallel jobs: several copies of the game at once (harness.multi_instance
# lets them start; run-autotest.sh then kills only its own on a timeout).
if [ "$jobs" != 1 ]; then
    base="$base,harness.multi_instance=ours"
    export OPENSC4_PARALLEL=1
fi
replace_a="$base${OPENSC4_PARITY_A:+,$OPENSC4_PARITY_A}"
replace_b="$base${OPENSC4_PARITY_B:+,$OPENSC4_PARITY_B}"
[ "$replace_a" != "$replace_b" ] || echo "run-parity: A and B are the same (a control run)" >&2
# The environment changes the heap layout (the C runtime copies each
# variable to the heap at startup) and the game's outcome depends on heap
# layout (docs/parity.md), so both runs get the same environment and the
# replacement lists go in an opensc4.ini each (OPENSC4_TEST_INI).
unset OPENSC4_REPLACE
mkdir -p "$out"
for side in a b; do
    list="$replace_a"
    [ "$side" = b ] && list="$replace_b"
    # game.no_kickout is on by default and changes the simulation: each side
    # runs it as the original unless its list names it (docs/no-kickout.md).
    case "$list" in *game.no_kickout=*) ;; *) list="$list,game.no_kickout=original" ;; esac
    { echo "[replacements]"; echo "$list" | tr ',' '\n'; } >"$out/$side.ini"
done

pause() { [ "$1" -gt 0 ] || return 0; timeout "$1" bash -c 'while :; do sleep 1; done' || true; }

# One pair of runs from <save> into <dir>; compares the two. Returns 0 when
# they agree, 1 when they differ, 2 when a run left no save.
run_pair() {
    local save="$1" city="$2" dir="$3" side
    mkdir -p "$dir"
    for side in a b; do
        [ -f "$dir/$side.sc4" ] && continue
        echo "run-parity: ${dir#"$out"/} $side"
        # Steam relaunches the game when it starts too soon after the last run.
        pause "$start_pause"
        "$repo/scripts/run-save-test.sh" "$build" "$dir/$side.sc4" "$days" \
            "OPENSC4_TEST_CITY=$city" "OPENSC4_TEST_START=$save" "OPENSC4_TEST_INI=$out/$side.ini" \
            OPENSC4_TEST_FINGERPRINT=1 >"$dir/$side.out" 2>&1 || true
    done
    [ -f "$dir/a.sc4" ] && [ -f "$dir/b.sc4" ] || return 2
    local fp_status=0 save_status=0
    python3 "$repo/tools/re/fpdiff.py" "$dir/a.log" "$dir/b.log" >"$dir/fpdiff.txt" || fp_status=$?
    python3 "$repo/tools/re/sc4save.py" --compare "$dir/a.sc4" "$dir/b.sc4" >"$dir/records.txt" || save_status=$?
    [ "$fp_status" = 0 ] && [ "$save_status" = 0 ]
}

# The verdict for one save, into <dir>/verdict.txt (first word: same,
# DIFFERENT or FAILED).
parity_save() {
    local save="$1" city tag dir try tdir status
    city="$(basename "$(dirname "$save")")"
    tag="$(basename "$save" .sc4)"
    dir="$out/$city/$tag"
    # Try 0 in <dir>, retries in <dir>/try1, try2... until B's outcomes are
    # all among A's.
    try=0
    while :; do
        tdir="$dir"
        [ "$try" = 0 ] || tdir="$dir/try$try"
        status=0
        run_pair "$save" "$city" "$tdir" || status=$?
        [ "$status" != 2 ] || break
        status=0
        python3 "$repo/tools/re/paritygroups.py" "$dir" >"$dir/groups.txt" || status=$?
        [ "$status" != 0 ] && [ "$try" -lt "$retries" ] || break
        try=$((try + 1))
    done
    if [ "$status" = 2 ]; then
        echo "$city/$tag: FAILED (no save; see ${tdir#"$out"/}/*.out)"
    elif [ "$status" = 0 ] && [ "$try" = 0 ]; then
        echo "$city/$tag: same ($(head -1 "$dir/fpdiff.txt"))"
    elif [ "$status" = 0 ]; then
        echo "$city/$tag: same after $((try + 1)) pairs ($(tail -1 "$dir/groups.txt")); first pair:"
        sed 's/^/    /' "$dir/groups.txt" "$dir/fpdiff.txt" "$dir/records.txt"
    else
        echo "$city/$tag: DIFFERENT after $((try + 1)) pairs ($(tail -1 "$dir/groups.txt")); first pair:"
        sed 's/^/    /' "$dir/groups.txt" "$dir/fpdiff.txt" "$dir/records.txt"
    fi >"$dir/verdict.txt"
    cat "$dir/verdict.txt"
}

all_saves=()
for save in "$saves"/*/*.sc4; do
    [ -f "$save" ] && all_saves+=("$save")
done

if [ "$jobs" = 1 ]; then
    # One job uses OPENSC4_PARITY_ENV too (it used to fall back to the shared
    # p4env folder, which another thread's batch may be using).
    [ -z "${OPENSC4_PARITY_ENV:-}" ] || export OPENSC4_SAVE_ENV="$OPENSC4_PARITY_ENV/job0"
    for save in "${all_saves[@]}"; do
        parity_save "$save"
    done
else
    # Each job takes the next save nobody has claimed yet (mkdir is atomic),
    # so a job is never idle while another still has a queue; in its own
    # user folder, and both runs of a pair stay in one job, so they get the
    # same environment. Starts are staggered so the copies do not all load
    # at once.
    claims="$out/.claims"
    rm -rf "$claims"
    mkdir -p "$claims"
    for ((k = 0; k < jobs; k++)); do
        (
            export OPENSC4_SAVE_ENV="${OPENSC4_PARITY_ENV:-$(cygpath "$LOCALAPPDATA")/opensc4/p4env}/job$k"
            [ "$k" = 0 ] || pause $((k * 15))
            for ((i = 0; i < ${#all_saves[@]}; i++)); do
                mkdir "$claims/$i" 2>/dev/null || continue
                parity_save "${all_saves[$i]}"
            done
        ) &
    done
    wait
    rm -rf "$claims"
fi

summary="$out/summary.txt"
printf 'days %s, retries %s, jobs %s, quick %s\nA: %s\nB: %s\n' "$days" "$retries" "$jobs" "$OPENSC4_TEST_QUICK" "$replace_a" "$replace_b" >"$summary"
same=0
differ=0
failed=0
for save in "${all_saves[@]}"; do
    verdict="$out/$(basename "$(dirname "$save")")/$(basename "$save" .sc4)/verdict.txt"
    if [ ! -f "$verdict" ]; then
        echo "$(basename "$(dirname "$save")")/$(basename "$save" .sc4): FAILED (no verdict)" >>"$summary"
        failed=$((failed + 1))
        continue
    fi
    cat "$verdict" >>"$summary"
    case "$(head -1 "$verdict")" in
        *": same"*) same=$((same + 1)) ;;
        *": DIFFERENT"*) differ=$((differ + 1)) ;;
        *) failed=$((failed + 1)) ;;
    esac
done
echo "run-parity: $same same, $differ different, $failed failed" | tee -a "$summary"
[ "$differ" = 0 ] && [ "$failed" = 0 ]
