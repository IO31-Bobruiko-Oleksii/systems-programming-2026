#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

CC="${CC:-cc}"
CFLAGS="-std=c11 -Wall -Wextra -Werror -g -O0 -I. -Ifakes -I../firmware/app/inc -I../firmware/devices/inc -I../firmware/platform/inc"
OUT=build

# "<test source without .c>|<code under test and fakes it links against>"
TESTS=(
  "test_servo|../firmware/devices/src/servo.c fakes/fake_pwm.c"
  "test_pots|../firmware/devices/src/pots.c fakes/fake_adc.c"
  "test_sdcard|../firmware/devices/src/sdcard.c fakes/fake_sdcard.c"
  "test_app|../firmware/app/src/app.c ../firmware/devices/src/servo.c ../firmware/devices/src/pots.c ../firmware/devices/src/sdcard.c fakes/fake_uart.c fakes/fake_pwm.c fakes/fake_adc.c fakes/fake_sdcard.c"
  "test_sd_replay|../firmware/devices/src/sdcard.c fakes/replay_spi.c"
)

rm -rf "$OUT"
mkdir -p "$OUT"
failed=0
for entry in "${TESTS[@]}"; do
  name="${entry%%|*}"
  srcs="${entry#*|}"
  echo "== $name"
  # shellcheck disable=SC2086
  $CC $CFLAGS "$name.c" $srcs -o "$OUT/$name"
  "./$OUT/$name" || failed=$((failed + 1))
done

if [ "$failed" -ne 0 ]; then
  echo "TEST FAILED: $failed test program(s)"
  exit 1
fi
echo "ALL TESTS PASSED"
