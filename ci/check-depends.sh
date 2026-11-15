#!/usr/bin/env bash
# ci/check-depends.sh: every mod's "depends" names a mod in this repository whose version
# (its manifest.json) the range accepts, by the port's rules (tgg-melee docs/manifest.md,
# "Dependencies"): "1.2.3" exactly, "1.2" any 1.2.x, "^1.2" the same leftmost non-zero part and at
# least 1.2, "~1.2" the same minor and at least 1.2, ">=1.2", "*". A range that has gone stale
# (a dependency moved on without the mods that need it) is a mod the port would refuse beside the
# rest of the official set. Exits 1 if any range doesn't match.
set -uo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)

# accepts RANGE VERSION: 0 when VERSION is in RANGE
accepts() {
    local range=$1 op= v1 v2 v3 r1 r2 r3 given
    [ "$range" = "*" ] && return 0
    case $range in
    ">="*) op=">="; range=${range#>=} ;;
    "^"*) op="^"; range=${range#^} ;;
    "~"*) op="~"; range=${range#\~} ;;
    esac
    IFS=. read -r v1 v2 v3 <<< "${2%%[-+]*}"
    IFS=. read -r r1 r2 r3 <<< "${range%%[-+]*}"
    given=$(( ${r2:+1} + ${r3:+1} + 1 ))
    r2=${r2:-0} r3=${r3:-0}
    local v=$(( v1 * 1000000 + v2 * 1000 + v3 )) r=$(( r1 * 1000000 + r2 * 1000 + r3 ))
    case $op in
    "") [ "$v1" = "$r1" ] && { [ $given -lt 2 ] || [ "$v2" = "$r2" ]; } && { [ $given -lt 3 ] || [ "$v3" = "$r3" ]; } ;;
    ">=") [ $v -ge $r ] ;;
    "~") [ $v -ge $r ] && [ "$v1" = "$r1" ] && [ "$v2" = "$r2" ] ;;
    "^")
        [ $v -ge $r ] || return 1
        if [ "$r1" != 0 ]; then [ "$v1" = "$r1" ]
        elif [ "$r2" != 0 ] || [ $given -lt 3 ]; then [ "$v1" = 0 ] && [ "$v2" = "$r2" ]
        else [ "$v1" = 0 ] && [ "$v2" = 0 ] && [ "$v3" = "$r3" ]
        fi ;;
    esac
}

declare -A version
for manifest in "$root"/*/manifest.json; do
    version[$(jq -r .id "$manifest")]=$(jq -r .version "$manifest")
done
bad=0 checked=0
for manifest in "$root"/*/manifest.json; do
    id=$(jq -r .id "$manifest")
    while IFS=$'\t' read -r dep range; do
        [ -n "$dep" ] || continue
        checked=$((checked + 1))
        have=${version[$dep]:-}
        if [ -z "$have" ]; then
            echo "$id: depends on $dep $range, which isn't in this repository"
            bad=$((bad + 1))
        elif ! accepts "$range" "$have"; then
            echo "$id: depends on $dep $range, and $dep is $have: the port would refuse $id"
            bad=$((bad + 1))
        fi
    done < <(jq -r '.depends // {} | to_entries[] | "\(.key)\t\(.value)"' "$manifest")
done
echo "dependencies: $checked checked, $bad out of range"
[ $bad = 0 ]
