#!/usr/bin/env bash
# Builds the fuzz drivers and runs each for SECONDS on JOBS processes.
# Corpora grow in OUT/<driver>/corpus; crashes, hangs and memory blowups land
# in OUT/<driver>/ as crash-*, timeout-* and oom-*.
#   nix develop .#fuzz --command tools/fuzz/run.sh SDK OUT [SECONDS] [JOBS] [driver...]
# Replays in $SLP_SEEDS (a folder) seed the replay-player driver.
set -euo pipefail

sdk=${1:?the SDK folder: <tgg-melee>/build/tgg-melee-sdk}
out=$(mkdir -p "${2:?an output folder}" && cd "$2" && pwd)
seconds=${3:-600}
jobs=${4:-4}
shift $(($# < 4 ? $# : 4))
drivers=("$@")
[ ${#drivers[@]} -gt 0 ] || drivers=(netplay replay-player http)
here=$(cd "$(dirname "$0")" && pwd)

"$here/build.sh" "$sdk" "${drivers[@]}"

for d in "${drivers[@]}"; do
    mkdir -p "$out/$d/corpus"
    seeds=()
    case $d in
    netplay) max_len=4096 ;;
    replay-player)
        max_len=65536
        [ -z "${SLP_SEEDS:-}" ] || seeds=("$SLP_SEEDS")
        ;;
    http) max_len=16384 ;;
    esac
    "$here/build/$d/fuzz" -fork="$jobs" -ignore_crashes=1 -ignore_timeouts=1 -ignore_ooms=1 \
        -max_total_time="$seconds" -max_len="$max_len" -timeout=10 -rss_limit_mb=2048 \
        -artifact_prefix="$out/$d/" "$out/$d/corpus" "${seeds[@]}" >"$out/$d/log" 2>&1 &
done
wait
failed=0
for d in "${drivers[@]}"; do
    found=$(find "$out/$d" -maxdepth 1 -name 'crash-*' -o -maxdepth 1 -name 'timeout-*' \
        -o -maxdepth 1 -name 'oom-*' | wc -l)
    echo "$d: $(grep -E 'cov:' "$out/$d/log" | tail -n 1 | cut -c1-90); $found found"
    [ "$found" -eq 0 ] || failed=1
done
exit $failed
