#!/usr/bin/env bash
# Make a test user folder's Plugins hold exactly one mod profile's files, or
# none: the files the last call put there are removed, the profile's are
# linked in (hard links; copies when the two folders are on different
# drives). Other files in the folder (OpenSC4.dll, opensc4.ini, the log) are
# never touched, and a mod file whose name is already taken by one of them
# stops the sync. Git Bash on Windows.
#
#   scripts/sync-mods.sh <Plugins folder> [<profile>]
#
# <profile> is a profile name (its Plugins folder from scripts/fetch-mods.sh,
# %LOCALAPPDATA%\opensc4\testenv\mods\<profile>\Plugins) or any folder that
# contains a slash. Without one the last profile's files are removed.
# <Plugins folder>/opensc4-mods.txt lists what is there: the source and a
# stamp of its files, then one file per line. An unchanged profile is not
# synced again.
set -euo pipefail

plugins="${1:?usage: sync-mods.sh <Plugins folder> [<profile>]}"
want="${2:-}"
manifest="$plugins/opensc4-mods.txt"

[ -n "$want" ] || [ -f "$manifest" ] || exit 0

src=""
stamp=""
if [ -n "$want" ]; then
    case "$want" in
        */*) src="$want" ;;
        *) src="${OPENSC4_MODS_ROOT:-$(cygpath "$LOCALAPPDATA")/opensc4/testenv/mods}/$want/Plugins" ;;
    esac
    [ -d "$src" ] || { echo "sync-mods: no $src; run scripts/fetch-mods.sh $want first" >&2; exit 1; }
    stamp="$(cd "$src" && find . -type f -printf '%P\t%s\t%T@\n' | sort | md5sum | cut -d' ' -f1)"
    [ ! -f "$manifest" ] || [ "$(head -1 "$manifest")" != "source $src $stamp" ] || exit 0
fi

if [ -f "$manifest" ]; then
    tail -n +2 "$manifest" | while IFS= read -r f; do
        rm -f "$plugins/$f"
    done
    # Folders the profile brought; the manifest goes last so a failed
    # removal is retried next time.
    tail -n +2 "$manifest" | sed -n 's|/[^/]*$||p' | sort -ru | while IFS= read -r d; do
        (cd "$plugins" && rmdir -p --ignore-fail-on-non-empty "$d" 2>/dev/null) || true
    done
    rm -f "$manifest"
fi
[ -n "$src" ] || { echo "sync-mods: removed the mods from $plugins"; exit 0; }

files="$(cd "$src" && find . -type f -printf '%P\n' | sort)"
clash="$(printf '%s\n' "$files" | while IFS= read -r f; do [ ! -e "$plugins/$f" ] || echo "$f"; done)"
[ -z "$clash" ] || { echo "sync-mods: already in $plugins, not from a profile:" >&2; echo "$clash" | head >&2; exit 1; }

# The list is written first, so a sync that fails halfway is cleaned up by
# the next one.
{ echo "source - -"; printf '%s\n' "$files"; } >"$manifest"
cp -al "$src/." "$plugins/" 2>/dev/null || cp -a "$src/." "$plugins/"
sed -i "1s|.*|source $src $stamp|" "$manifest"
echo "sync-mods: $want ($(printf '%s\n' "$files" | wc -l) files) in $plugins"
