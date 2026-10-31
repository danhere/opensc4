#!/usr/bin/env bash
# Play SimCity 4 with OpenSC4 in its own user folder (%LOCALAPPDATA%\opensc4\
# playenv\userdir), apart from the one the automated tests use, so neither
# disturbs the other. Settings are in that folder's Plugins\opensc4.ini,
# written with defaults on first use. Git Bash on Windows; the desktop
# shortcut runs this.
#
#   scripts/play.sh [build dir]     default: %LOCALAPPDATA%\opensc4\build\debug
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$(cygpath "$LOCALAPPDATA")/opensc4/build/debug}"
local_data="$(cygpath "$LOCALAPPDATA")/opensc4"
playenv="$local_data/playenv/userdir"
testenv="$local_data/testenv/userdir"

if [ ! -d "$playenv/Plugins" ]; then
    # The test folder holds a copy of a region and the game's settings file
    # (run-autotest.sh seeds it from the user's own SimCity 4 folder).
    [ -d "$testenv/Regions" ] || { echo "play: seed the test folder first (scripts/run-autotest.sh)" >&2; exit 1; }
    echo "Setting up $playenv"
    mkdir -p "$playenv/Plugins"
    cp -r "$testenv/Regions" "$playenv/"
    cp "$testenv/SimCity 4.cfg" "$playenv/"
    cat >"$playenv/Plugins/opensc4.ini" <<'EOF'
; OpenSC4 settings for scripts/play.sh (see docs/dev-environment.md, "Settings").

[renderer]
scale = 1.5             ; interface drawn 1.5 times as big (1 to 4); the city keeps its size
resolution = desktop    ; or WxH in screen pixels, e.g. 2560x1440
window = fullscreen     ; windowed, fullscreen or borderless
vsync = on
EOF
fi

# Test runs may be going in their own folders (harness.multi_instance gives
# each its own lock); only a game already playing in this folder stops us.
# The folder's last log names that game's pid.
pid="$(sed -n 's/.*core\.dll .* attached: pid \([0-9]*\),.*/\1/p' "$playenv/Plugins/opensc4.log" 2>/dev/null | head -1 || true)"
if [ -n "$pid" ] && tasklist //FI "PID eq $pid" //FI "IMAGENAME eq SimCity 4.exe" //NH | grep -q "SimCity 4.exe"; then
    echo "SimCity 4 is already running from this play folder; close it first" >&2
    read -rp "Press Enter to close this window" _
    exit 1
fi

# No -w: opensc4.ini decides the window (run-game.sh's default is windowed).
status=0
OPENSC4_TESTENV="$playenv" "$repo/scripts/run-game.sh" "$build" "OPENSC4_GAME_WINDOW= " || status=$?
if [ "$status" -ne 0 ]; then
    echo "The game exited with code $status; the log is $(cygpath -w "$playenv/Plugins/opensc4.log")"
    read -rp "Press Enter to close this window" _
fi
exit "$status"
