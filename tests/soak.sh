#!/usr/bin/env bash
# Host soak: the real application code on fake time, S2-like load, hours of virtual time.
# usage: tests/soak.sh [hours]        (default 2; takes under a second)
set -euo pipefail
cd "$(dirname "$0")"

CC="${CC:-cc}"
OUT=build
mkdir -p "$OUT"
$CC -std=c11 -O1 -Wall -Wextra -Werror -g -I. -Ifakes \
    -I../firmware/app/inc -I../firmware/devices/inc -I../firmware/platform/inc \
    soak.c ../firmware/app/src/app.c ../firmware/devices/src/servo.c \
    ../firmware/devices/src/pots.c ../firmware/devices/src/sdcard.c \
    ../firmware/devices/src/perf.c ../firmware/devices/src/tracker.c \
    ../firmware/devices/src/logger.c fakes/fake_uart.c fakes/fake_pwm.c fakes/fake_adc.c \
    fakes/fake_sdcard.c fakes/fake_time.c fakes/fake_button.c fakes/fake_mem.c \
    fakes/fake_probe.c -o "$OUT/soak"
"./$OUT/soak" "${1:-2}"
