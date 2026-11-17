#!/usr/bin/env bash
# Builds the fuzz drivers with clang, libFuzzer, ASan and UBSan, against
# tgg-melee's SDK, into tools/fuzz/build/. Run it in the fuzz shell:
#   nix develop .#fuzz --command tools/fuzz/build.sh <tgg-melee>/build/tgg-melee-sdk [driver...]
set -euo pipefail

sdk=$(realpath "${1:?the SDK folder: <tgg-melee>/build/tgg-melee-sdk}")
shift
here=$(cd "$(dirname "$0")" && pwd)
mods=$(cd "$here/../.." && pwd)
out="$here/build"
mkdir -p "$out"
drivers=("$@")
[ ${#drivers[@]} -gt 0 ] || drivers=(netplay replay-player http)

# The mods' own flags (TggMod.cmake), with -O1 and the sanitizers. -fwrapv as
# in the real build: signed overflow wraps there, so it isn't a finding.
common=(
    -DMTX_USE_PS -DTARGET_PC -Dbool=int
    -isystem "$sdk/include/game/0" -isystem "$sdk/include/game/1" -isystem "$sdk/game"
    -isystem "$sdk/include/game/3" -isystem "$sdk/include"
    -include "$sdk/include/tgg/game_imports.h" -include "$sdk/include/tgg/internal/glibc.h"
    -include "$sdk/include/tgg/game/tier.h"
    -std=gnu17 -fno-strict-aliasing -fwrapv -ffp-contract=off -g -O1
    -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined
    -Wno-incompatible-pointer-types -Wno-unknown-warning-option
)

build() {
    local name=$1 self=$2 mod=$3
    shift 3
    local srcs=("$@")
    local objs=()
    mkdir -p "$out/$name"
    for src in "${srcs[@]}" "$here/port.c" "$here/$name.c"; do
        local obj="$out/$name/$(basename "$src" .c).o"
        clang "${common[@]}" "-DTGG_SELF_$self=1" -I "$mods/$mod/include" \
            -fsanitize=fuzzer-no-link -c "$src" -o "$obj"
        objs+=("$obj")
    done
    clang -fsanitize=fuzzer,address,undefined "${objs[@]}" -lm -o "$out/$name/fuzz"
    echo "built $out/$name/fuzz"
}

for d in "${drivers[@]}"; do
    case $d in
    netplay)
        build netplay tgg_netplay netplay \
            "$mods"/netplay/src/{net,session,match,wire,stun,turn,fakenet}.c
        ;;
    replay-player)
        build replay-player tgg_replay_player replay-player \
            "$mods"/replay-player/src/{slp,summary,setup}.c
        ;;
    http)
        # conn.c (TCP and TLS) is the driver's own; Mbed TLS only for the
        # WebSocket key's SHA-1 and base64
        build http tgg_http http \
            "$mods"/http/src/{request,ws,buf,url}.c \
            "$mods"/http/src/mbedtls/{base64,sha1,platform_util,constant_time}.c
        ;;
    *)
        echo "no driver $d" >&2
        exit 2
        ;;
    esac
done
