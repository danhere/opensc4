#!/usr/bin/env bash
# Fills a test Plugins folder with the probe plugin DLLs (tests/probe_plugin.cpp)
# in the arrangement docs/plugin-dlls.md describes. Git Bash on Windows.
#
#   scripts/make-dll-probes.sh <build dir> <Plugins folder>
#
# Never point this at the game's install folder or a real user folder.
set -euo pipefail
bin="${1:?usage: make-dll-probes.sh <build dir> <Plugins folder>}/bin"
plugins="${2:?usage: make-dll-probes.sh <build dir> <Plugins folder>}"
mkdir -p "$plugins/sub"
rm -f "$plugins"/probe_*.dll "$plugins"/probe_*.ini "$plugins"/sub/probe_*.dll
for n in a b c d e f g h i; do
    cp "$bin/probe_$n.dll" "$plugins/"
done
cp "$bin/probe_e.dll" "$plugins/probe_k.dll"   # the same DLL under a second name
cp "$bin/probe_j.dll" "$plugins/sub/"          # a subfolder: not scanned
printf '[Admin]\r\nEnabled=false\r\n' > "$plugins/probe_c.ini"
printf '[Admin]\r\nEnabled=0\r\n' > "$plugins/probe_d.ini"
printf '[Admin]\r\nEnabled=true\r\n' > "$plugins/probe_e.ini"
printf '[Admin]\r\nEnabled=FALSE\r\n' > "$plugins/probe_k.ini"
printf '[Other]\r\nEnabled=false\r\n' > "$plugins/probe_i.ini"
