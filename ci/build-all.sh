#!/usr/bin/env bash
# ci/build-all.sh <SDK folder> [CMake toolchain file]: builds every mod (each
# folder with a manifest.json) against the SDK, into <mod>/build/, and says
# which failed. Exits 1 if any did.
set -uo pipefail
sdk=$(cd "${1:?usage: ci/build-all.sh <SDK folder> [toolchain file]}" && pwd)
toolchain=${2:+-DCMAKE_TOOLCHAIN_FILE=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")}
root=$(cd "$(dirname "$0")/.." && pwd)
failed=()
for manifest in "$root"/*/manifest.json; do
    mod=$(dirname "$manifest")
    name=$(basename "$mod")
    echo "::group::$name"
    if cmake -S "$mod" -B "$mod/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTGG_SDK="$sdk" $toolchain &&
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
