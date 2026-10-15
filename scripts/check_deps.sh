#!/usr/bin/env bash
set -u
FW="$(cd "$(dirname "${BASH_SOURCE[0]}")/../firmware" && pwd)"
violations=0
files=0

hdrs() {
  local d
  for d in "$@"; do
    [ -d "$FW/$d" ] && find "$FW/$d" -maxdepth 1 -name '*.h' -printf '%f\n'
  done | sort -u
}

includes_of() {
  sed -n 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*\([<"][^>"]*\).*/\1/p' "$1"
}

check() {
  local layer=$1 scan=$2 mode=$3 list_dirs=$4
  local list d f inc name
  list=$(hdrs $list_dirs)
  for d in $scan; do
    [ -d "$FW/$d" ] || continue
    while IFS= read -r f; do
      files=$((files + 1))
      while IFS= read -r inc; do
        [ -z "$inc" ] && continue
        name=$(basename "${inc#?}")
        if [ "${inc:0:1}" = "<" ]; then
          if [ "$mode" = allow ] && echo "$name" | grep -qE '^(stm32|core_cm|cmsis|arm_)'; then
            echo "VIOLATION [$layer] ${f#"$FW"/}: includes $inc>"
            violations=$((violations + 1))
          fi
        else
          if [ "$mode" = allow ] && ! echo "$list" | grep -qxF "$name"; then
            echo "VIOLATION [$layer] ${f#"$FW"/}: includes \"$name\", not allowed in this layer"
            violations=$((violations + 1))
          elif [ "$mode" = deny ] && echo "$list" | grep -qxF "$name"; then
            echo "VIOLATION [$layer] ${f#"$FW"/}: includes \"$name\", an upper-layer header"
            violations=$((violations + 1))
          fi
        fi
      done < <(includes_of "$f")
    done < <(find "$FW/$d" -maxdepth 1 \( -name '*.c' -o -name '*.h' \) | sort)
  done
}

check platform-inc "platform/inc" allow "platform/inc"
check platform-src "platform/src" deny  "devices/inc app/inc"
check devices      "devices/inc devices/src" allow "devices/inc platform/inc"
check app          "app/inc app/src"         allow "app/inc devices/inc platform/inc"

if [ "$violations" -ne 0 ]; then
  echo "check-deps FAILED: $violations violation(s) in $files file(s)"
  exit 1
fi
echo "check-deps OK: $files file(s), no layer violations"
