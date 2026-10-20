#!/usr/bin/env bash
# Play SimCity 4 with core.dll, in the isolated test user folder that
# run-autotest.sh uses (seed it with one autotest run first). Git Bash on Windows.
#
#   scripts/run-game.sh <build dir> [env assignments...]
#
# Example: scripts/run-game.sh "$LOCALAPPDATA/opensc4/build/debug" OPENSC4_REPLACE=renderer.d3d11=ours
# core.dll's log ends up in <test user folder>/Plugins/opensc4.log.
# OPENSC4_GAME_ARGS adds game options; OPENSC4_GAME_WINDOW replaces the default -w
# (e.g. "-f" for full screen).
# OPENSC4_MUTE=1 starts the game with its sound off (the game's own -audio:off).
set -euo pipefail

build="${1:?usage: run-game.sh <build dir> [VAR=value...]}"
shift
for assignment in "$@"; do
    export "${assignment?}"
done

repo="$(cd "$(dirname "$0")/.." && pwd)"
mute_arg=""
[ "${OPENSC4_MUTE:-0}" = 1 ] && mute_arg="-audio:off"
game="${SC4_GAME:-C:/Program Files (x86)/Steam/steamapps/common/SimCity 4 Deluxe/Apps/SimCity 4.exe}"
testenv="${OPENSC4_TESTENV:-$(cygpath "$LOCALAPPDATA")/opensc4/testenv/userdir}"
[ -d "$testenv/Plugins" ] || { echo "run-game: $testenv not seeded; run scripts/run-autotest.sh once" >&2; exit 1; }

SC4_PLUGINS="$testenv/Plugins" "$repo/scripts/deploy-plugin.sh" "$build" >/dev/null
"$build/bin/opensc4.exe" --core "$testenv/Plugins/OpenSC4.dll" --game "$game" \
    -- ${OPENSC4_GAME_WINDOW:--w} -intro:off ${mute_arg} "-UserDir:$(cygpath -d "$testenv")\\" ${OPENSC4_GAME_ARGS:-}
