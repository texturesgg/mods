#!/usr/bin/env bash
# ci/build-all.sh <SDK folder> [CMake toolchain file]: builds every mod (each folder with a
# manifest.json) against the SDK, into <mod>/build/mods/<id>/, and says which failed. Exits 1 if
# any did.
#
# A C mod builds with its own CMake. A script mod ("script": true) has nothing to compile: it's
# checked with the SDK's tools/tgg-script, as the port checks it at load (refused means failed),
# its package record is written to build/package.json (tgg-script pack), and its files are laid out
# in build/mods/<id>/ as the port loads them. The SDK's luau/tgg.luau goes to build/tgg.luau, which
# the mod's .luaurc names, and with luau-lsp on PATH its scripts are type-checked against it
# (--!strict). An SDK whose tgg-script doesn't run here (a Windows SDK on Linux) only lays out.
set -uo pipefail
sdk=$(cd "${1:?usage: ci/build-all.sh <SDK folder> [toolchain file]}" && pwd)
toolchain=${2:+-DCMAKE_TOOLCHAIN_FILE=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")}
root=$(cd "$(dirname "$0")/.." && pwd)
tool=$sdk/tools/tgg-script
"$tool" > /dev/null 2>&1; [ $? = 2 ] || tool=
lsp=$(command -v luau-lsp)
failed=()

script_mod() { # script_mod <mod folder>: check, pack, lay out and type-check one script mod
    local mod=$1 id out
    checked=
    id=$(jq -r .id "$mod/manifest.json")
    out=$mod/build/mods/$id
    rm -rf "$mod/build"
    mkdir -p "$out"
    cp -r "$mod/manifest.json" "$mod/scripts" "$out/"
    for d in assets files; do
        [ -d "$mod/$d" ] && cp -r "$mod/$d" "$out/"
    done
    cp "$sdk/luau/tgg.luau" "$mod/build/tgg.luau"
    if [ -n "$tool" ]; then
        "$tool" check "$mod" || return 1
        "$tool" pack "$mod" > "$mod/build/package.json" || return 1
    else
        echo "not checked: this SDK's tools/tgg-script doesn't run here"
        checked=" by layout only: tgg-script didn't run"
    fi
    if [ -n "$lsp" ]; then
        (cd "$mod" && "$lsp" analyze --platform standard scripts/*.luau) || return 1
        echo "types: clean"
    fi
}

for manifest in "$root"/*/manifest.json; do
    mod=$(dirname "$manifest")
    name=$(basename "$mod")
    echo "::group::$name"
    if [ "$(jq -r '.script // false' "$manifest")" = true ]; then
        if script_mod "$mod"; then
            echo "$name: checked${checked}"
        else
            failed+=("$name")
        fi
    elif cmake -S "$mod" -B "$mod/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTGG_SDK="$sdk" $toolchain &&
        cmake --build "$mod/build"; then
        echo "$name: built"
    else
        failed+=("$name")
    fi
    echo "::endgroup::"
done
if [ ${#failed[@]} -gt 0 ]; then
    echo "didn't build: ${failed[*]}"
    exit 1
fi
echo "every mod built"
