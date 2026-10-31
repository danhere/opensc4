#!/usr/bin/env bash
# Download and install the mod test profiles (plans/mod-testing.md) with
# sc4pac, each into its own Plugins folder under
# %LOCALAPPDATA%\opensc4\testenv\mods\<profile>\Plugins. Nothing goes into the
# repo, the game's install folder or the user's own Plugins folder. Git Bash on
# Windows; needs Java 17+ on PATH.
#
#   scripts/fetch-mods.sh [--check] <profile>...
#
# A profile is tools/mods/<profile>.txt, one directive per line:
#   include <profile>        everything of another profile first
#   pkg <group:name> [st]    an sc4pac package; "st" = needs Simtropolis
#   drop <group:name>        leave out a package an include added
#   variant <id> <value>     an sc4pac variant choice (the last one wins)
#   direct <name> <url> <sha256> [st]
#                            a mod that is not in sc4pac: a .zip whose .dll and
#                            .ini files go into the Plugins root, or a single .dll
# Every variant a profile's packages ask for must be chosen there: sc4pac does
# not ask when it runs without a terminal, it stops.
#
# Simtropolis needs a personal token (community.simtropolis.com/sc4pac/my-token/)
# in SC4PAC_SIMTROPOLIS_TOKEN or in %LOCALAPPDATA%\opensc4\secrets\simtropolis-token.
# Without one the "st" entries are skipped and listed as skipped.
#
# --check reads the profiles and prints what would be installed and the
# sc4pac configuration, without downloading anything (also on Linux).
#
# sc4pac (GPL-3) is downloaded once, checked against its sha256, into
# %LOCALAPPDATA%\opensc4\tools\sc4pac-<version>. The download cache is shared by
# all profiles (testenv\mods\.cache), so a file is fetched once. sc4pac checks
# every file it downloads against the sha256 in its channel.
#
# Each profile folder gets profile.txt: what was installed, what was skipped,
# the DLLs in its Plugins root and its size. Use a profile in a test run with
# OPENSC4_TEST_MODS=<profile> (scripts/run-autotest.sh).
set -euo pipefail

check=0
if [ "${1:-}" = --check ]; then
    check=1
    shift
fi
[ $# -gt 0 ] || { echo "usage: fetch-mods.sh [--check] <profile>..." >&2; exit 2; }

repo="$(cd "$(dirname "$0")/.." && pwd)"
profiles_dir="$repo/tools/mods"
if command -v cygpath >/dev/null; then
    local_data="$(cygpath "$LOCALAPPDATA")/opensc4"
    winpath() { cygpath -m "$1"; }
else
    local_data="${OPENSC4_LOCAL_DATA:-$HOME/.local/share/opensc4}"
    winpath() { echo "$1"; }
fi
mods_root="${OPENSC4_MODS_ROOT:-$local_data/testenv/mods}"

sc4pac_version=0.10.1
sc4pac_sha256=8f6876c42107cfe0b58c11db175cd25af4365a4ed8b4d5ccafbde0774787297f
sc4pac_url="https://github.com/memo33/sc4pac-tools/releases/download/$sc4pac_version/sc4pac-$sc4pac_version.zip"
sc4pac_dir="$local_data/tools/sc4pac-$sc4pac_version"
channels=(
    "https://memo33.github.io/sc4pac/channel/"
    "https://sc4pac.simtropolis.com/"
    "https://sc4evermore.github.io/sc4pac-channel/channel/"
)

token="${SC4PAC_SIMTROPOLIS_TOKEN:-}"
token_file="$local_data/secrets/simtropolis-token"
[ -n "$token" ] || [ ! -f "$token_file" ] || token="$(tr -d '\r\n' <"$token_file")"

# Filled by read_profile.
declare -A pkg_st variants
pkgs=()
directs=()
reading=()

read_profile() {
    local name="$1" file="$profiles_dir/$1.txt" kind a b c d p
    [ -f "$file" ] || { echo "fetch-mods: no profile $file" >&2; exit 1; }
    for p in "${reading[@]}"; do
        [ "$p" != "$name" ] || { echo "fetch-mods: $name includes itself" >&2; exit 1; }
    done
    reading+=("$name")
    while read -r kind a b c d; do
        case "$kind" in
            "" | "#"*) ;;
            include) read_profile "$a" ;;
            pkg)
                [ -n "${pkg_st[$a]+x}" ] || pkgs+=("$a")
                pkg_st[$a]="${b:-}"
                ;;
            drop)
                local kept=()
                for p in "${pkgs[@]}"; do [ "$p" = "$a" ] || kept+=("$p"); done
                pkgs=("${kept[@]}")
                unset "pkg_st[$a]"
                ;;
            variant) variants[$a]="$b" ;;
            direct) directs+=("$a $b $c ${d:-}") ;;
            *) echo "fetch-mods: $file: unknown directive '$kind'" >&2; exit 1 ;;
        esac
    done < <(tr -d '\r' <"$file")
    unset 'reading[${#reading[@]}-1]'
}

json_string() { printf '"%s"' "$(printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g')"; }

# Writes sc4pac-plugins.json: the whole configuration, rewritten every run, so
# a package taken out of a profile is uninstalled by the next update.
write_spec() {
    local out="$1" plugins="$2" cache="$3" first=1 k p
    {
        printf '{\n  "config": {\n'
        printf '    "pluginsRoot": %s,\n' "$(json_string "$(winpath "$plugins")")"
        printf '    "cacheRoot": %s,\n' "$(json_string "$(winpath "$cache")")"
        printf '    "tempRoot": "temp",\n    "variant": {'
        for k in $(printf '%s\n' "${!variants[@]}" | sort); do
            [ $first = 1 ] || printf ','
            first=0
            printf '\n      %s: %s' "$(json_string "$k")" "$(json_string "${variants[$k]}")"
        done
        printf '\n    },\n    "channels": ['
        first=1
        for p in "${channels[@]}"; do
            [ $first = 1 ] || printf ','
            first=0
            printf '\n      %s' "$(json_string "$p")"
        done
        printf '\n    ]\n  },\n  "explicit": ['
        first=1
        for p in "${install[@]}"; do
            [ $first = 1 ] || printf ','
            first=0
            printf '\n    %s' "$(json_string "$p")"
        done
        printf '\n  ]\n}\n'
    } >"$out"
}

ensure_sc4pac() {
    command -v java >/dev/null || {
        echo "fetch-mods: java not found; install a Java 17+ runtime (e.g. winget install EclipseAdoptium.Temurin.21.JRE) and open a new shell" >&2
        exit 1
    }
    [ -f "$sc4pac_dir/sc4pac-cli.jar" ] && return
    local zip="$local_data/tools/sc4pac-$sc4pac_version.zip"
    mkdir -p "$local_data/tools"
    curl -fsSL -o "$zip" "$sc4pac_url"
    echo "$sc4pac_sha256  $zip" | sha256sum -c --quiet - || { rm -f "$zip"; echo "fetch-mods: sc4pac zip checksum mismatch" >&2; exit 1; }
    rm -rf "$sc4pac_dir"
    unzip -q "$zip" -d "$sc4pac_dir"
    rm -f "$zip"
}

run_sc4pac() {
    # Same options as the release's sc4pac.bat. cicdec unpacks the NAM's
    # installer. Without a terminal sc4pac answers its own questions with
    # yes; --yes covers the DLL and Lua confirmations too.
    SC4PAC_CICDEC_CMD="$(winpath "$sc4pac_dir/cicdec/cicdec.exe")" \
    SC4PAC_MAX_PATH_LENGTH=260 \
    SC4PAC_NO_COLOR=1 \
    SC4PAC_SIMTROPOLIS_TOKEN="$token" \
        java -XX:+IgnoreUnrecognizedVMOptions --sun-misc-unsafe-memory-access=allow --enable-native-access=ALL-UNNAMED \
        -jar "$(winpath "$sc4pac_dir/sc4pac-cli.jar")" "$@"
}

install_direct() {
    local plugins="$1" cache="$2" name="$3" url="$4" sha="$5" file dl tmp
    file="$(basename "$url")"
    dl="$cache/direct/$name/$file"
    if [ ! -f "$dl" ] || ! echo "$sha  $dl" | sha256sum -c --quiet - 2>/dev/null; then
        mkdir -p "$(dirname "$dl")"
        curl -fsSL -o "$dl" "$url"
        echo "$sha  $dl" | sha256sum -c --quiet - || { rm -f "$dl"; echo "fetch-mods: $name: checksum mismatch" >&2; exit 1; }
    fi
    case "$file" in
        *.dll | *.DLL) cp "$dl" "$plugins/" ;;
        *.zip)
            tmp="$(mktemp -d)"
            unzip -q "$dl" -d "$tmp"
            find "$tmp" -type f \( -iname '*.dll' -o -iname '*.ini' \) -exec cp {} "$plugins/" \;
            rm -rf "$tmp"
            ;;
        *) echo "fetch-mods: $name: not a .zip or .dll: $file" >&2; exit 1 ;;
    esac
    echo "$name $url" >>"$plugins/../direct.txt"
}

for profile in "$@"; do
    pkg_st=()
    variants=()
    pkgs=()
    directs=()
    reading=()
    read_profile "$profile"

    install=()
    skipped=()
    for p in "${pkgs[@]}"; do
        if [ "${pkg_st[$p]}" = st ] && [ -z "$token" ]; then
            skipped+=("$p")
        else
            install+=("$p")
        fi
    done
    install_directs=()
    for d in "${directs[@]}"; do
        set -- $d
        if [ "${4:-}" = st ] && [ -z "$token" ]; then
            skipped+=("$1")
        else
            install_directs+=("$d")
        fi
    done

    dir="$mods_root/$profile"
    cache="$mods_root/.cache"
    if [ $check = 1 ]; then
        echo "== $profile -> $dir"
        echo "install: ${install[*]:-(none)}"
        echo "direct: ${install_directs[*]:-(none)}"
        echo "skipped (needs Simtropolis token): ${skipped[*]:-(none)}"
        write_spec /dev/stdout "$dir/Plugins" "$cache"
        continue
    fi

    ensure_sc4pac
    mkdir -p "$dir/Plugins" "$cache"
    write_spec "$dir/sc4pac-plugins.json" "$dir/Plugins" "$cache"
    echo "fetch-mods: $profile: sc4pac update (log: $dir/update.log)"
    (cd "$dir" && run_sc4pac update --yes) >"$dir/update.log" 2>&1 || {
        tail -20 "$dir/update.log" >&2
        echo "fetch-mods: $profile: sc4pac update failed; see $dir/update.log" >&2
        exit 1
    }
    rm -f "$dir/direct.txt"
    for d in "${install_directs[@]}"; do
        install_direct "$dir/Plugins" "$cache" $d
    done

    {
        echo "profile $profile, $(date -u +%Y-%m-%dT%H:%M:%SZ), sc4pac $sc4pac_version"
        echo "skipped (needs Simtropolis token): ${skipped[*]:-none}"
        echo "size: $(du -sh "$dir/Plugins" | cut -f1), files: $(find "$dir/Plugins" -type f | wc -l)"
        echo "dlls:"
        find "$dir/Plugins" -maxdepth 1 -iname '*.dll' -printf '  %f\n' | sort
        echo "installed:"
        (cd "$dir" && run_sc4pac list) 2>/dev/null | sed 's/^/  /'
        [ ! -f "$dir/direct.txt" ] || sed 's/^/  direct /' "$dir/direct.txt"
    } >"$dir/profile.txt"
    cat "$dir/profile.txt"
done
