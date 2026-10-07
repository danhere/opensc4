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

repo="$(cd "$(dirname "$0")/.." && pwd)"
base="determinism.fixed_seed=ours,determinism.virtual_clock=ours,determinism.fixed_ticks=ours"
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
    { echo "[replacements]"; echo "$list" | tr ',' '\n'; } >"$out/$side.ini"
done

pause() { timeout "$1" bash -c 'while :; do sleep 1; done' || true; }

# One pair of runs from <save> into <dir>; compares the two. Returns 0 when
# they agree, 1 when they differ, 2 when a run left no save.
run_pair() {
    local save="$1" city="$2" dir="$3" side
    mkdir -p "$dir"
    for side in a b; do
        [ -f "$dir/$side.sc4" ] && continue
        echo "run-parity: ${dir#"$out"/} $side"
        # Steam relaunches the game when it starts too soon after the last run.
        pause 20
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

mkdir -p "$out"
summary="$out/summary.txt"
printf 'days %s, retries %s\nA: %s\nB: %s\n' "$days" "$retries" "$replace_a" "$replace_b" >"$summary"
same=0
differ=0
failed=0
for save in "$saves"/*/*.sc4; do
    [ -f "$save" ] || continue
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
        echo "$city/$tag: FAILED (no save; see ${tdir#"$out"/}/*.out)" | tee -a "$summary"
        failed=$((failed + 1))
    elif [ "$status" = 0 ] && [ "$try" = 0 ]; then
        echo "$city/$tag: same ($(head -1 "$dir/fpdiff.txt"))" | tee -a "$summary"
        same=$((same + 1))
    elif [ "$status" = 0 ]; then
        echo "$city/$tag: same after $((try + 1)) pairs ($(tail -1 "$dir/groups.txt")); first pair:" | tee -a "$summary"
        sed 's/^/    /' "$dir/groups.txt" "$dir/fpdiff.txt" "$dir/records.txt" | tee -a "$summary"
        same=$((same + 1))
    else
        echo "$city/$tag: DIFFERENT after $((try + 1)) pairs ($(tail -1 "$dir/groups.txt")); first pair:" | tee -a "$summary"
        sed 's/^/    /' "$dir/groups.txt" "$dir/fpdiff.txt" "$dir/records.txt" | tee -a "$summary"
        differ=$((differ + 1))
    fi
done
echo "run-parity: $same same, $differ different, $failed failed" | tee -a "$summary"
[ "$differ" = 0 ] && [ "$failed" = 0 ]
