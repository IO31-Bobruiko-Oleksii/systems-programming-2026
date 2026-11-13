#!/usr/bin/env bash
# Builds the firmware in the Docker image with -fstack-usage and writes the memory analysis to
# docs/metrics/memory.txt. The toolchain is the one of `make build`, so the numbers are those of
# the firmware that runs in the simulator. The measured values (STAT mem static, the largest
# stack_used of S1, S2, S3) are only compared with the build, they do not change it.
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE="${IMAGE:-stm32-build}"
out=docs/metrics/memory.txt
tmp=.lr3-mem

mkdir -p docs/metrics
docker image inspect "$IMAGE" > /dev/null 2>&1 || make image
rm -rf "$tmp"
docker run --rm -v "$PWD":/workspace -w /workspace --user "$(id -u):$(id -g)" "$IMAGE" bash -c "
    set -e
    make -C firmware -f build.mk all BUILD_DIR=/workspace/$tmp OPT='-Og -fstack-usage' > /dev/null
    python3 -I tools/lr3_memory.py /workspace/$tmp firmware/STM32C031xx_FLASH.ld \
        --measured-static 2296 --measured-stack 940 \
        --irq SysTick=3 --irq USART2=1 --irq EXTI=0
" | tee "$out"
rm -rf "$tmp"
