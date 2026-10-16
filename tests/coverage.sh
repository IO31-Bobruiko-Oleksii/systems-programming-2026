#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
cd "$(dirname "$0")"

CC="${CC:-cc}"
MIN="${COVERAGE_MIN:-90}"
OUT=build-cov
CFLAGS="-std=c11 -Wall -Wextra -Werror -g -O0 --coverage -I. -Ifakes -I../firmware/app/inc -I../firmware/devices/inc -I../firmware/platform/inc"
LAYERS="../firmware/devices/src ../firmware/app/src"

eval "$(tr -d '\r' < run.sh | sed -n '/^TESTS=(/,/^)/p')"

rm -rf "$OUT"
mkdir -p "$OUT/obj" "$OUT/report"

units=""
for entry in "${TESTS[@]}"; do
  units="$units ${entry#*|}"
done
for src in $(printf '%s\n' $units | sort -u); do
  $CC $CFLAGS -c "$src" -o "$OUT/obj/$(basename "${src%.c}").o"
done

for entry in "${TESTS[@]}"; do
  name="${entry%%|*}"
  srcs="${entry#*|}"
  objs=""
  for src in $srcs; do
    objs="$objs $OUT/obj/$(basename "${src%.c}").o"
  done
  $CC $CFLAGS -c "$name.c" -o "$OUT/obj/$name.o"
  $CC --coverage "$OUT/obj/$name.o" $objs -o "$OUT/$name"
  "./$OUT/$name" > "$OUT/$name.log" || { echo "test program $name failed, see tests/$OUT/$name.log"; exit 1; }
done

pick() {
  awk -v want="$(basename "$2")" -v label="$3" '
    /^File /          { keep = (index($0, "/" want "\047") > 0 || index($0, "\047" want "\047") > 0) }
    keep && index($0, label ":") == 1 { sub(label ":", ""); split($0, a, "% of "); print a[1], a[2]; exit }
  ' "$1"
}

printf '%-22s %6s %8s %8s   %8s %8s\n' "file" "lines" "covered" "percent" "branches" "taken"
tot=0; cov=0; btot=0; bcov=0
for dir in $LAYERS; do
  for src in "$dir"/*.c; do
    base="$(basename "${src%.c}")"
    gcov -b -o "$OUT/obj" "$src" > "$OUT/report/$base.txt" 2>&1
    mv -f ./*.gcov "$OUT/report/"
    read -r lp ln <<< "$(pick "$OUT/report/$base.txt" "$src" "Lines executed")"
    read -r bp bn <<< "$(pick "$OUT/report/$base.txt" "$src" "Taken at least once")"
    lc="$(awk -v p="$lp" -v n="$ln" 'BEGIN { printf "%d", (p * n / 100) + 0.5 }')"
    bc="$(awk -v p="$bp" -v n="$bn" 'BEGIN { printf "%d", (p * n / 100) + 0.5 }')"
    printf '%-22s %6d %8d %7.2f%%   %8d %7.2f%%\n' "${src#../firmware/}" "$ln" "$lc" "$lp" "$bn" "$bp"
    tot=$((tot + ln)); cov=$((cov + lc)); btot=$((btot + bn)); bcov=$((bcov + bc))
  done
done
total_pct="$(awk -v c="$cov" -v t="$tot" 'BEGIN { printf "%.2f", (t ? 100 * c / t : 0) }')"
branch_pct="$(awk -v c="$bcov" -v t="$btot" 'BEGIN { printf "%.2f", (t ? 100 * c / t : 0) }')"
printf '%-22s %6d %8d %7s%%   %8d %7s%%\n' "total" "$tot" "$cov" "$total_pct" "$btot" "$branch_pct"
echo "annotated sources (lines marked #####  were never executed): tests/$OUT/report/*.gcov"
if awk -v p="$total_pct" -v m="$MIN" 'BEGIN { exit !(p + 0 < m + 0) }'; then
  echo "COVERAGE TOO LOW: lines $total_pct% < $MIN%"
  exit 1
fi
echo "COVERAGE OK: lines $total_pct% >= $MIN%, branches $branch_pct% (not gated)"
