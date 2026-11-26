#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

name="${1:?usage: $0 <scenario> <loop|event|sweep|flood|burst> [timeout_ms]}"
mode="${2:?usage: $0 <scenario> <loop|event|sweep|flood|burst> [timeout_ms]}"
timeout_ms="${3:-20000}"

scn="scenarios/$name.yaml"
[ -r "$scn" ] || { echo "lr3_capture: no such scenario: $scn" >&2; exit 2; }
cap="docs/captures/lr3"
met="docs/metrics"
if [ -n "${LR_TAG:-}" ]; then
    cap="docs/captures/lr4/$LR_TAG"
    met="docs/metrics/lr4/$LR_TAG"
fi
elf_args=()
if [ -n "${LR_ELF:-}" ]; then
    [ -r "$LR_ELF" ] || { echo "lr3_capture: no such firmware: $LR_ELF" >&2; exit 2; }
    elf_args=(--elf "$LR_ELF")
fi
echo "lr3_capture $name: tag=${LR_TAG:-lr3} elf=${LR_ELF:-firmware/build/firmware.elf} sha256=$(sha256sum "${LR_ELF:-firmware/build/firmware.elf}" | cut -c1-12)"
mkdir -p "$cap" "$met"
diagram="scenarios/lr3.diagram.json"
[ -r "scenarios/$name.diagram.json" ] && diagram="scenarios/$name.diagram.json"
vcd="$cap/$name.vcd"
serial="$cap/$name.serial.log"
report="$met/analyzer-$name.txt"
rm -f "$vcd" "$serial" "$report"

log="$(mktemp)"
rc=0
wokwi-cli --timeout "$timeout_ms" --scenario "$scn" --diagram-file "$diagram" \
    ${elf_args[@]+"${elf_args[@]}"} \
    --vcd-file "$vcd" --serial-log-file "$serial" 2>&1 | tee "$log" || rc=$?

ok=1
[ "$rc" -eq 0 ] || ok=0
grep -q "Scenario completed successfully" "$log" || ok=0
[ -s "$vcd" ] || ok=0
rm -f "$log"
if [ "$ok" -ne 1 ]; then
    echo "lr3_capture $name: simulator run FAILED (rc=$rc)"
    exit 1
fi

if [ "$mode" = "sweep" ]; then
    python3 tools/lr3_sweep.py "$vcd" --serial "$serial" | tee "$report"
elif [ "$mode" = "burst" ]; then
    python3 tools/lr3_burst.py "$vcd" --serial "$serial" | tee "$report"
elif [ "$mode" = "flood" ]; then
    python3 tools/lr3_sweep.py "$vcd" --serial "$serial" --names 1=stats-reply,2=sd-write | tee "$report"
else
    python3 tools/lr3_analyze.py "$vcd" "$mode" --serial "$serial" | tee "$report"
fi
