#!/usr/bin/env bash
# Run SimCity 4 unattended with core.dll: load a city, run the simulation,
# quit without saving, and print core.dll's log. Git Bash on Windows.
#
#   scripts/run-autotest.sh <build dir> [env assignments...]
#
# Examples:
#   scripts/run-autotest.sh "$LOCALAPPDATA/opensc4/build/debug"
#   scripts/run-autotest.sh "$LOCALAPPDATA/opensc4/build/debug" OPENSC4_REPLACE=simulators.pollution.proxy=ours OPENSC4_TEST_SECONDS=120
#
# The game runs with -UserDir pointing at an isolated test folder
# (OPENSC4_TESTENV, default %LOCALAPPDATA%\opensc4\testenv\userdir) whose
# Plugins folder holds only OpenSC4.dll, so the user's own plugins and saves
# are never loaded or touched. On first use the folder is seeded with a copy of
# OPENSC4_TEST_REGION (default Timbuktu) from the user's SimCity 4 folder.
#
# See src/core/autotest.h for the OPENSC4_TEST_* variables. Defaults here:
# OPENSC4_TEST_CITY="Making Money Tutorial", OPENSC4_TEST_SECONDS=60.
# OPENSC4_GAME_ARGS adds game command-line options, e.g. "-d:DirectX".
# OPENSC4_GAME_WINDOW replaces the default -w, e.g. "-f -r1024x768x32" for full screen.
# Other copies of the game may be running (other threads' tests, parallel
# parity jobs): every run gets harness.multi_instance=ours (unless
# OPENSC4_REPLACE names it), and only a game still running in this same test
# folder stops the run. OPENSC4_PARALLEL is no longer needed.
# OPENSC4_LAUNCHER_ARGS adds launcher options, e.g. "--large-address".
set -euo pipefail

build="${1:?usage: run-autotest.sh <build dir> [VAR=value...]}"
shift
for assignment in "$@"; do
    export "${assignment?}"
done

repo="$(cd "$(dirname "$0")/.." && pwd)"
game="${SC4_GAME:-C:/Program Files (x86)/Steam/steamapps/common/SimCity 4 Deluxe/Apps/SimCity 4.exe}"
user_sc4="$(cygpath "$USERPROFILE")/Documents/SimCity 4"
testenv="${OPENSC4_TESTENV:-$(cygpath "$LOCALAPPDATA")/opensc4/testenv/userdir}"
region="${OPENSC4_TEST_REGION:-Timbuktu}"
timeout_s="${OPENSC4_TEST_TIMEOUT:-600}"

export OPENSC4_TEST_CITY="${OPENSC4_TEST_CITY:-Making Money Tutorial}"
export OPENSC4_TEST_SECONDS="${OPENSC4_TEST_SECONDS:-60}"

if [ ! -d "$testenv/Regions/$region" ]; then
    echo "Seeding $testenv with region $region"
    mkdir -p "$testenv/Plugins" "$testenv/Regions"
    cp -r "$user_sc4/Regions/$region" "$testenv/Regions/"
    # The .cfg remembers the last region, so the game opens in it.
    cp "$user_sc4/SimCity 4.cfg" "$testenv/"
fi

# Several copies at once: the game's single-instance mutex gets a
# per-process name (src/core/multi_instance.cpp). Copies started without it
# keep the plain name, which this one then never opens.
case ",${OPENSC4_REPLACE:-}," in
*,harness.multi_instance=*) ;;
*) export OPENSC4_REPLACE="${OPENSC4_REPLACE:+$OPENSC4_REPLACE,}harness.multi_instance=ours" ;;
esac

# Two games must not share a user folder. The folder's last log names the
# pid of the game that wrote it; refuse while that game still runs.
log="$testenv/Plugins/opensc4.log"
pid="$(sed -n 's/.*core\.dll .* attached: pid \([0-9]*\),.*/\1/p' "$log" 2>/dev/null | head -1 || true)"
if [ -n "$pid" ] && tasklist //FI "PID eq $pid" //FI "IMAGENAME eq SimCity 4.exe" //NH | grep -q "SimCity 4.exe"; then
    echo "SimCity 4 (pid $pid) is already running in $testenv; wait for it or use another OPENSC4_TESTENV" >&2
    exit 1
fi

SC4_PLUGINS="$testenv/Plugins" "$repo/scripts/deploy-plugin.sh" "$build" >/dev/null
rm -f "$testenv/Plugins/opensc4.log" "$testenv/Plugins/opensc4-coverage.csv"
# Function list for diagnostics.coverage (written by scripts/ghidra-setup.sh).
functions="${OPENSC4_ANALYSIS_DIR:-$(cygpath "$LOCALAPPDATA")/opensc4/analysis}/functions.csv"
[ -f "$functions" ] && cp "$functions" "$testenv/Plugins/opensc4-functions.csv"

# 8.3 path: no spaces, so no quoting around the trailing backslash SC4 wants.
userdir="$(cygpath -d "$testenv")\\"

# Watchdog. Its sleep must be stopped together with it: a leftover sleep keeps
# this script's stdout open (hanging any pipe it feeds) and would later kill
# whatever SimCity 4 is running when it fires. It kills this run's game only,
# by the pid core.dll logs on attach, never by name (other copies may run).
(
    trap 'kill "$timer" 2>/dev/null; exit 0' TERM
    sleep "$timeout_s" &
    timer=$!
    wait "$timer"
    pid="$(sed -n 's/.*core\.dll .* attached: pid \([0-9]*\),.*/\1/p' "$log" 2>/dev/null | head -1 || true)"
    [ -n "$pid" ] && taskkill //PID "$pid" //F && echo "run-autotest: timed out after ${timeout_s}s; killed the game" >>"$log"
) </dev/null >/dev/null 2>&1 &
watchdog=$!
status=0
"$build/bin/opensc4.exe" --wait ${OPENSC4_LAUNCHER_ARGS:-} --core "$testenv/Plugins/OpenSC4.dll" --game "$game" \
    -- ${OPENSC4_GAME_WINDOW:--w} -intro:off "-UserDir:$userdir" ${OPENSC4_GAME_ARGS:-} || status=$?
kill "$watchdog" 2>/dev/null || true
wait "$watchdog" 2>/dev/null || true

if ! grep -q "director: OnStart" "$log" 2>/dev/null; then
    # Steam's DRM stub relaunched the game through Steam (it does this when
    # Steam still has a previous session open); that copy has no core.dll.
    echo "run-autotest: core.dll's director never started; the game was probably relaunched by Steam." >&2
    echo "run-autotest: close any SimCity 4 window, wait a few seconds and retry." >&2
    status=1
fi
cat "$log" 2>/dev/null | grep -v "autotest: region city pop" || true
exit "$status"
