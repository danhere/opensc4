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
# OPENSC4_TEST_REGION picks the region: a folder in the test folder's Regions
# (default Timbuktu); its name is written into the run's SimCity 4.cfg
# (tools/cfg_region.py) so the game opens it.
# OPENSC4_TEST_START=<file.sc4> starts from that save of the same city instead
# of the region's own copy (it replaces the city's file after the refresh).
# OPENSC4_TEST_START_PAUSED=1 loads the city paused; the counted days start
# when it is resumed from the game's speed buttons (to build something first).
# OPENSC4_TEST_INI=<file> becomes the run's Plugins/opensc4.ini (otherwise
# there is none): a way to pass replacements without changing the
# environment, which changes the game's heap layout (docs/parity.md).
# core.dll's log of the run is copied to <output>.log. Compare outputs with:
# opensc4-dbpf diff <a.sc4> <b.sc4>
# OPENSC4_SAVE_ENV replaces the disposable user folder (one per parallel job).
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
env="${OPENSC4_SAVE_ENV:-$local_data/p4env/userdir}"
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
    # The .cfg remembers the last region (the fixture's: Timbuktu), and the
    # game opens that one; another region is written into the copy.
    [ "$region" = Timbuktu ] ||
        python3 "$repo/tools/cfg_region.py" "$fixture/SimCity 4.cfg" "$env/SimCity 4.cfg" "$region"
    [ -z "${OPENSC4_TEST_START:-}" ] || cp "$OPENSC4_TEST_START" "$env/Regions/$region/City - $city.sc4"
    rm -f "$env/Plugins/opensc4.ini"
    [ -z "${OPENSC4_TEST_INI:-}" ] || cp "$OPENSC4_TEST_INI" "$env/Plugins/opensc4.ini"
    # One game at a time (unless OPENSC4_PARALLEL): wait for another copy
    # (another batch's) to end, up to OPENSC4_SAVE_WAIT seconds (default 600).
    # The last run's log goes first: run-autotest.sh refuses to start while
    # a game runs and leaves the log alone, and an old log read as this run's
    # once copied the unchanged start file out as the run's save.
    waited=0
    while [ -z "${OPENSC4_PARALLEL:-}" ] && [ "$waited" -lt "${OPENSC4_SAVE_WAIT:-600}" ] &&
        tasklist //FI "IMAGENAME eq SimCity 4.exe" //NH | grep -q "SimCity 4.exe"; do
        pause 5
        waited=$((waited + 5))
    done
    rm -f "$log" 2>/dev/null || true
    OPENSC4_TESTENV="$env" "$repo/scripts/run-autotest.sh" "$build" "OPENSC4_TEST_DAYS=$days" OPENSC4_TEST_SAVE=1 \
        >"$env/run.out" 2>&1 || true
    grep -q "director: OnStart" "$log" 2>/dev/null && break
    echo "run-save-test: the game did not start (Steam relaunch?), retrying; last output:" >&2
    tail -3 "$env/run.out" >&2
    pause 30
done
cp "$log" "${out%.sc4}.log"
grep "autotest: running\|autotest: stopped\|SaveCity" "$log" | sed 's/^[^]]*] //'
if ! grep -qF "\City - $city.sc4 (pop" "$log"; then
    echo "run-save-test: the log does not show City - $city.sc4 loaded; see $log" >&2
    exit 1
fi
if ! grep -q "SaveCity ok" "$log"; then
    echo "run-save-test: no save; see $log" >&2
    exit 1
fi
cp "$env/Regions/$region/City - $city.sc4" "$out"
echo "saved: $out"
