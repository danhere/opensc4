#!/usr/bin/env bash
# Download the MSVC CRT and Windows SDK (x86 only) with xwin into .xwin/sysroot,
# in the winsysroot layout that cmake/toolchain-clang-cl.cmake expects.
#
# Runs on Linux and in Git Bash on Windows. Requires xwin on PATH
# (Windows: scoop install tools/scoop/xwin.json; Linux: cargo install xwin).
#
# This accepts Microsoft's license for the downloaded components; read it at
# https://go.microsoft.com/fwlink/?LinkId=2086102 before running.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
# Same variable the toolchain file reads; defaults to .xwin/sysroot in the repo.
sysroot="${OPENSC4_WINSYSROOT:-$root/.xwin/sysroot}"
cache="$(dirname "$sysroot")/cache"

if ! command -v xwin >/dev/null; then
    echo "xwin not found on PATH" >&2
    exit 1
fi

xwin --accept-license \
    --cache-dir "$cache" \
    --arch x86 \
    splat \
    --use-winsysroot-style \
    --preserve-ms-arch-notation \
    --output "$sysroot"

echo "Windows SDK/CRT ready in $sysroot"
