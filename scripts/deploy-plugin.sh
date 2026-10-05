#!/usr/bin/env bash
# Install or remove core.dll as a GZCOM plugin (OpenSC4.dll) in the user's
# SimCity 4 Plugins folder, so the game calls its director. Git Bash on Windows.
#
#   scripts/deploy-plugin.sh <build dir>     install <build dir>/bin/core.dll
#   scripts/deploy-plugin.sh --remove        remove OpenSC4.dll and its log
#
# Launch with the installed copy so the injected and plugin-loaded modules are
# the same file:  opensc4.exe --core "<Plugins>/OpenSC4.dll" ...
# SC4_PLUGINS overrides the Plugins folder.
set -euo pipefail

plugins="${SC4_PLUGINS:-$(cygpath "$USERPROFILE")/Documents/SimCity 4/Plugins}"
target="$plugins/OpenSC4.dll"

if [ ! -d "$plugins" ]; then
    echo "Plugins folder not found: $plugins" >&2
    exit 1
fi

if [ "${1:-}" = "--remove" ]; then
    rm -f "$target" "$plugins/opensc4.log"
    echo "Removed $target"
    exit 0
fi

build="${1:?usage: deploy-plugin.sh <build dir> | --remove}"
cp "$build/bin/core.dll" "$target"
echo "Installed $target"
