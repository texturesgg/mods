#!/usr/bin/env bash
# ci/tier-report.sh <SDK folder> [report.json]: each mod's API tier, with the
# reasons, and the numbers on what the mods use of the game
# (tgg-melee-goals/api-strategy.md, "What the mods really use"). Run after
# ci/build-all.sh: what a mod names of the game comes from its build's import
# list (<mod>/build/tgg-mod-<id>-imports.c), which is exactly the game
# functions and variables it links against.
#
# A mod is game tier when it includes a game header (melee/, sysdolphin/,
# dolphin/), has a hook record, uses TGG_GAME_SYMBOL, TGG_GAME_FN or
# TGG_ORIGINAL, includes tgg/game/, or has a TGG_FIGHTER or TGG_STAGE record.
# Otherwise it's stable. It never fails: the report is for people, and the
# JSON for the next run to compare against.
set -uo pipefail
sdk=$(cd "${1:?usage: ci/tier-report.sh <SDK folder> [report.json]}" && pwd)
json_out=${2:-}
root=$(cd "$(dirname "$0")/.." && pwd)
symbols="$sdk/symbols.txt"

# Game functions and variables by name, and whether each has a console address
# (the SDK's symbols.txt has an addr= column from tgg-melee's identity work on)
declare -A game_fn game_data has_addr
while read -r kind name rest; do
    case $kind in
    func) [[ " $rest " == *" runtime "* ]] || game_fn[$name]=1 ;;
    data) game_data[$name]=1 ;;
    *) continue ;;
    esac
    [[ $rest == *addr=* ]] && has_addr[$name]=1
done < <(grep -E '^(func|data) ' "$symbols")
addr_known=$(grep -cE '^(func|data) .*addr=' "$symbols")

sources() { find "$1" -path "$1/build" -prune -o \( -name '*.c' -o -name '*.h' \) -type f -print | sort; }

declare -A all_headers all_fns all_data all_hooked all_fields
mods=0 hooking=0 hook_records=0 with_header=0 stable=0 game=0
no_header=() rows=()
for manifest in "$root"/*/manifest.json; do
    dir=$(dirname "$manifest")
    id=$(jq -r .id "$manifest")
    mods=$((mods + 1))
    mapfile -t files < <(sources "$dir")
    reasons=()
    [ ${#files[@]} -gt 0 ] || files=(/dev/null)

    # Game headers
    mapfile -t headers < <(grep -hoE '#[[:space:]]*include[[:space:]]*[<"](melee|sysdolphin|dolphin)/[^>"]+' "${files[@]}" |
        sed -E 's/.*[<"]//' | sort -u)
    for h in "${headers[@]}"; do all_headers[$h]=1; done
    if [ ${#headers[@]} -gt 0 ]; then
        with_header=$((with_header + 1))
        first=$(grep -HnE '#[[:space:]]*include[[:space:]]*[<"](melee|sysdolphin|dolphin)/' "${files[@]}" | head -1)
        first=${first#"$dir/"}
        reasons+=("includes ${#headers[@]} game header(s), first $(echo "$first" | sed -E 's/^([^:]+:[0-9]+):.*[<"]([^>"]+).*/\2 (\1)/')")
    else
        no_header+=("$id")
    fi

    # Hook records: TGG_BEFORE(sym, fn) and the _NAMED forms ("file.c:name", fn)
    mapfile -t hooks < <(grep -hoE 'TGG_(BEFORE|AFTER|REPLACE)(_NAMED)?[[:space:]]*\([[:space:]]*("[^"]+"|[A-Za-z_][A-Za-z0-9_]*)' "${files[@]}" |
        sed -E 's/.*\([[:space:]]*"?//; s/"$//')
    if [ ${#hooks[@]} -gt 0 ]; then
        hooking=$((hooking + 1))
        hook_records=$((hook_records + ${#hooks[@]}))
        for h in "${hooks[@]}"; do all_hooked[$h]=1; done
        reasons+=("${#hooks[@]} hook record(s), first ${hooks[0]}")
    fi

    # The rest of what makes a mod game tier
    for marker in TGG_GAME_SYMBOL TGG_GAME_FN TGG_ORIGINAL TGG_FIGHTER TGG_STAGE 'tgg/game/'; do
        if grep -qE "${marker}" "${files[@]}" 2>/dev/null; then reasons+=("uses ${marker}"); fi
    done

    # What it links against, from its build's import list
    imports=$(ls "$dir"/build/tgg-mod-*-imports.c 2>/dev/null | head -1)
    if [ -n "$imports" ]; then
        while read -r fn; do [ -n "${game_fn[$fn]:-}" ] && all_fns[$fn]=1; done < <(sed -nE 's/^TGG_IMPORT_FN\(([^)]+)\)/\1/p' "$imports")
        while read -r v; do [ -n "${game_data[$v]:-}" ] && all_data[$v]=1; done < <(sed -nE 's/^TGG_IMPORT_DATA\(([^)]+)\)/\1/p' "$imports")
    fi
    # And variables reached by name: TGG_GAME_SYMBOL(pointer, "name")
    while read -r v; do [ -n "$v" ] && all_data[$v]=1; done < <(grep -hoE 'TGG_GAME_SYMBOL\([^,]+,[[:space:]]*"[^"]+"' "${files[@]}" | sed -E 's/.*"([^"]+)"/\1/')

    # Fighter fields: members reached through a variable declared as a
    # Fighter* or set from GET_FIGHTER, or through GET_FIGHTER(...) itself, with
    # their members ("dmg.x1830_percent"). An approximation, from the source.
    mapfile -t vars < <(grep -hoE '(Fighter|ftCo_Fighter)[[:space:]]*\*[[:space:]]*[A-Za-z_][A-Za-z0-9_]*|[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=[[:space:]]*GET_FIGHTER' "${files[@]}" |
        sed -E 's/^(Fighter|ftCo_Fighter)[[:space:]]*\*[[:space:]]*//; s/[[:space:]]*=.*//' | sort -u)
    for v in "${vars[@]}"; do
        while read -r f; do [ -n "$f" ] && all_fields[$f]=1; done < <(grep -hoE "\\b${v}->[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)*" "${files[@]}" | sed -E 's/.*->//')
    done
    while read -r f; do [ -n "$f" ] && all_fields[$f]=1; done < <(grep -hoE 'GET_FIGHTER\([^()]*\)->[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*' "${files[@]}" | sed -E 's/.*->//')

    if [ ${#reasons[@]} -gt 0 ]; then
        game=$((game + 1))
        rows+=("$(jq -cn --arg id "$id" --argjson r "$(printf '%s\n' "${reasons[@]}" | jq -R . | jq -s .)" '{id: $id, tier: "game", reasons: $r}')")
    else
        stable=$((stable + 1))
        rows+=("$(jq -cn --arg id "$id" '{id: $id, tier: "stable", reasons: []}')")
    fi
done

address_named=0 with_addr=0
for h in "${!all_hooked[@]}"; do
    [[ $h =~ _8[0-9A-Fa-f]{7}$ ]] && address_named=$((address_named + 1))
    [ -n "${has_addr[${h##*:}]:-}" ] || [ -n "${has_addr[$h]:-}" ] && with_addr=$((with_addr + 1))
done
for h in "${!all_hooked[@]}"; do [ -n "${game_fn[$h]:-}" ] || [[ $h == *:* ]] || continue; all_fns[$h]=1; done

echo "== Tiers"
printf '%s\n' "${rows[@]}" | jq -r '"\(.id): \(.tier)" + (if .tier == "game" then "\n  " + (.reasons | join("\n  ")) else "" end)'
echo
echo "== What the mods use"
printf '%-58s %s\n' "Mods" "$mods" \
    "Stable / game tier" "$stable / $game" \
    "Mods that hook game functions" "$hooking of $mods" \
    "Hook records" "$hook_records" \
    "Distinct functions hooked" "${#all_hooked[@]}, of which $address_named have address names" \
    "Hooked functions with a console address" "$( [ "$addr_known" -gt 0 ] && echo "$with_addr of ${#all_hooked[@]}" || echo "not in this SDK's symbols.txt yet" )" \
    "Mods that include a game header" "$with_header of $mods" \
    "Distinct game headers included" "${#all_headers[@]}" \
    "Distinct game functions named (called or hooked)" "${#all_fns[@]}" \
    "Game variables named" "${#all_data[@]}" \
    "Distinct fighter fields read or written (approximate)" "${#all_fields[@]}" \
    "Mods with no game header" "${#no_header[@]}: $(printf '%s\n' "${no_header[@]}" | sed 's/^tgg\.//' | paste -sd, - | sed 's/,/, /g')"

if [ -n "$json_out" ]; then
    printf '%s\n' "${rows[@]}" | jq -s --argjson summary "$(jq -n \
        --argjson mods "$mods" --argjson stable "$stable" --argjson game "$game" \
        --argjson hooking "$hooking" --argjson records "$hook_records" \
        --argjson hooked "$(printf '%s\n' "${!all_hooked[@]}" | sort | jq -R . | jq -s .)" \
        --argjson headers "$(printf '%s\n' "${!all_headers[@]}" | sort | jq -R . | jq -s .)" \
        --argjson functions "$(printf '%s\n' "${!all_fns[@]}" | sort | jq -R . | jq -s .)" \
        --argjson variables "$(printf '%s\n' "${!all_data[@]}" | sort | jq -R . | jq -s .)" \
        --argjson fields "$(printf '%s\n' "${!all_fields[@]}" | sort | jq -R . | jq -s .)" \
        '{mods: $mods, stable: $stable, game: $game, hooking: $hooking, hook_records: $records,
          hooked: $hooked, headers: $headers, functions: $functions, variables: $variables,
          fighter_fields: $fields}')" '{mods: ., summary: $summary}' > "$json_out"
fi
exit 0
