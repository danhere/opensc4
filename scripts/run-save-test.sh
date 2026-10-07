#!/usr/bin/env bash
# P4 save test: load a city, run it for a number of game days (0: none),
# save it in place, quit, and copy the saved city to <output .sc4>. Runs in a
# disposable user folder (%LOCALAPPDATA%\opensc4\p4env\userdir) refreshed
# from the test folder's region before every run, because saving rewrites the
# city and its neighbours. Git Bash on Windows.
#
#   scripts/run-save-test.sh <build dir> <output .sc4> <days> [env assignments...]
#
# OPENSC4_TEST_CITY picks the city (default "Making Money Tutorial").
# OPENSC4_TEST_START=<file.sc4> starts from that save of the same city instead
# of the region's own copy (it replaces the city's file after the refresh).
# OPENSC4_TEST_INI=<file> becomes the run's Plugins/opensc4.ini (otherwise
# there is none): a way to pass replacements without changing the
# environment, which changes the game's heap layout (docs/parity.md).
# core.dll's log of the run is copied to <output>.log. Compare outputs with:
# opensc4-dbpf diff <a.sc4> <b.sc4>
set -euo pipefail

build="${1:?usage: run-save-test.sh <build dir> <output .sc4> <days> [VAR=value...]}"
out="${2:?usage: run-save-test.sh <build dir> <output .sc4> <days> [VAR=value...]}"
days="${3:?usage: run-save-test.sh <build dir> <output .sc4> <days> [VAR=value...]}"
shift 3
for assignment in "$@"; do
    export "${assignment?}"
done

repo="$(cd "$(dirname "$0")/.." && pwd)"
local_data="$(cygpath "$LOCALAPPDATA")/opensc4"
fixture="$local_data/testenv/userdir"
env="$local_data/p4env/userdir"
region="${OPENSC4_TEST_REGION:-Timbuktu}"
city="${OPENSC4_TEST_CITY:-Making Money Tutorial}"
export OPENSC4_TEST_CITY="$city"

[ -d "$fixture/Regions/$region" ] || { echo "run-save-test: no $fixture/Regions/$region; run run-autotest.sh once first" >&2; exit 1; }
[ -z "${OPENSC4_TEST_START:-}" ] || [ -f "$OPENSC4_TEST_START" ] || { echo "run-save-test: no $OPENSC4_TEST_START" >&2; exit 1; }
[ -z "${OPENSC4_TEST_INI:-}" ] || [ -f "$OPENSC4_TEST_INI" ] || { echo "run-save-test: no $OPENSC4_TEST_INI" >&2; exit 1; }

pause() { timeout "$1" bash -c 'while :; do sleep 1; done' || true; }

log="$env/Plugins/opensc4.log"
for try in 1 2 3; do
    # Fresh region every run: the previous run's save changed it.
    mkdir -p "$env/Plugins" "$env/Regions/$region"
    cp -r "$fixture/Regions/$region/." "$env/Regions/$region/"
    cp "$fixture/SimCity 4.cfg" "$env/"
    [ -z "${OPENSC4_TEST_START:-}" ] || cp "$OPENSC4_TEST_START" "$env/Regions/$region/City - $city.sc4"
    rm -f "$env/Plugins/opensc4.ini"
    [ -z "${OPENSC4_TEST_INI:-}" ] || cp "$OPENSC4_TEST_INI" "$env/Plugins/opensc4.ini"
    OPENSC4_TESTENV="$env" "$repo/scripts/run-autotest.sh" "$build" "OPENSC4_TEST_DAYS=$days" OPENSC4_TEST_SAVE=1 \
        >"$env/run.out" 2>&1 || true
    grep -q "director: OnStart" "$log" 2>/dev/null && break
    echo "run-save-test: the game did not start (Steam relaunch?), retrying; last output:" >&2
    tail -3 "$env/run.out" >&2
    pause 30
done
cp "$log" "${out%.sc4}.log"
grep "autotest: running\|autotest: stopped\|SaveCity" "$log" | sed 's/^[^]]*] //'
if ! grep -q "SaveCity ok" "$log"; then
    echo "run-save-test: no save; see $log" >&2
    exit 1
fi
cp "$env/Regions/$region/City - $city.sc4" "$out"
echo "saved: $out"
