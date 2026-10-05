#!/usr/bin/env bash
# Build the OpenSC4 Ghidra project for SimCity 4.exe 1.1.641 and run the
# analysis pipeline. Git Bash on Windows; needs Ghidra (scoop) and python3.
#
#   scripts/ghidra-setup.sh            run every step on the existing project
#   scripts/ghidra-setup.sh --fresh    delete the project and start over
#
# Steps:
#   1. copy the exe to a path without parentheses (analyzeHeadless.bat
#      breaks on "Program Files (x86)") and import it with full auto-analysis
#      (only if the project does not exist yet; about 3 minutes);
#   2. censuses on the auto-analysed program, before anything is renamed:
#      class registrations, class creation, simulator vtable bypasses;
#   3. the exe's ID-to-name table and gzcom-dll's interface slot table;
#   4. sc4-ghidra-symbols: Function ID databases and data type archives;
#   5. names for classes, vtables and interface methods.
#
# Environment:
#   SC4_GAME              path to SimCity 4.exe (default: Steam install)
#   OPENSC4_GHIDRA_DIR    project folder (default %LOCALAPPDATA%\opensc4\ghidra)
#   OPENSC4_ANALYSIS_DIR  output folder  (default %LOCALAPPDATA%\opensc4\analysis)
#   SC4_GHIDRA_SYMBOLS    sc4-ghidra-symbols clone (default ~/Projects/ref in WSL)
#
# The project and every output describe the game's code: never commit them.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
game="${SC4_GAME:-C:/Program Files (x86)/Steam/steamapps/common/SimCity 4 Deluxe/Apps/SimCity 4.exe}"
local_root="$(cygpath "$LOCALAPPDATA")/opensc4"
project_dir="${OPENSC4_GHIDRA_DIR:-$local_root/ghidra}"
out="${OPENSC4_ANALYSIS_DIR:-$local_root/analysis}"
symbols="${SC4_GHIDRA_SYMBOLS:-//wsl.localhost/Ubuntu/home/dan/Projects/ref/sc4-ghidra-symbols}"
project=SC4_1.1.641
program="SimCity 4.exe"

# Short (8.3) paths: Git Bash passes .bat arguments to cmd.exe unquoted, and
# Ghidra cannot read scripts from \\wsl.localhost paths given from Git Bash.
headless="$(cygpath -d "$(cygpath "$USERPROFILE")/scoop/apps/ghidra/current/support/analyzeHeadless.bat")"
scripts="$local_root/ghidra-scripts"

step() { printf '\n== %s\n' "$*"; }

run_script() { # run_script <-readOnly|--write> <script> [args...]
    local mode="$1" script="$2"
    shift 2
    local readonly_flag=()
    [ "$mode" = "-readOnly" ] && readonly_flag=(-readOnly)
    local log="$out/${script%.java}.log"
    "$headless" "$(cygpath -d "$project_dir")" "$project" -process "$program" -noanalysis "${readonly_flag[@]}" \
        -scriptPath "$(cygpath -d "$scripts")" -postScript "$script" "$@" >"$log" 2>&1 || true
    if grep -qE "ERROR REPORT SCRIPT ERROR|Abort due to" "$log"; then
        grep -E "ERROR" "$log" | head -5 >&2
        echo "ghidra-setup: $script failed; see $log" >&2
        exit 1
    fi
    grep -E "${script%.java}.java> " "$log" | sed -E 's/^INFO +[A-Za-z]+\.java> //; s/ \(GhidraScript\) *$//' | grep -v "declares" || true
}

win() { cygpath -w "$1"; }

[ "${1:-}" = "--fresh" ] && rm -rf "$project_dir/$project.rep" "$project_dir/$project.gpr"
mkdir -p "$project_dir/input" "$out" "$scripts"
cp "$repo"/tools/ghidra/*.java "$scripts/"

step "1. import"
if [ ! -f "$project_dir/$project.gpr" ]; then
    cp "$game" "$project_dir/input/$program"
    "$headless" "$(cygpath -d "$project_dir")" "$project" -import "$(cygpath -d "$project_dir/input")\\$program" \
        >"$out/import.log" 2>&1
    grep -E "REPORT: (Analysis|Import|Save) succeeded" "$out/import.log" || { echo "import failed; see $out/import.log" >&2; exit 1; }
else
    echo "project exists ($project_dir); pass --fresh to rebuild"
fi

step "2. censuses"
run_script -readOnly ClassRegistrationCensus.java "$(win "$out/class-registrations.csv")"
run_script -readOnly ClassCreationCensus.java "$(win "$out/class-registrations.csv")" "$(win "$out/class-creation.csv")"

step "3. address database, name and slot tables"
python3 "$repo/tools/gen_addresses.py" --check "$repo/data/addresses-641.toml" "$project_dir/input/$program"
python3 "$repo/tools/re/extract_name_table.py" "$project_dir/input/$program" >"$out/name-table.csv"
python3 "$repo/tools/re/gzcom_vtable_slots.py" "$repo/third_party/gzcom-dll/gzcom-dll/include" >"$out/interface-slots.csv"
{ echo "clsid,name"; grep -E ",cSC4[A-Za-z]*Simulator$" "$out/name-table.csv" | sort -t, -k2 | awk -F, '!seen[$1]++'; } \
    >"$out/simulators.csv"
run_script -readOnly VtableBypassCensus.java "$(win "$out/class-registrations.csv")" "$(win "$out/simulators.csv")" \
    "$(win "$out/simulator-bypass.csv")" | tail -1

step "4. sc4-ghidra-symbols"
# Local copy, for the same \\wsl.localhost reason as the scripts.
local_symbols="$local_root/sc4-ghidra-symbols"
mkdir -p "$local_symbols/function-id"
cp "$symbols"/function-id/*.fidb "$local_symbols/function-id/"
cp "$symbols"/SimCity4.gdt "$symbols"/Gimex.gdt "$symbols"/Lua5.0.gdt "$local_symbols/"
run_script --write ImportSc4Symbols.java "$(cygpath -d "$local_symbols")"

step "5. names"
run_script --write ApplyKnownNames.java "$(win "$out/class-registrations.csv")" "$(win "$out/name-table.csv")" \
    "$(win "$out/interface-slots.csv")"

echo
echo "Done. Project: $project_dir/$project.gpr; outputs: $out"
