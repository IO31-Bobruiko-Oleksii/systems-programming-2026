#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

name="${1:?usage: $0 <scenario> <loop|event> [timeout_ms]}"
mode="${2:?usage: $0 <scenario> <loop|event> [timeout_ms]}"
timeout_ms="${3:-20000}"

scn="scenarios/$name.yaml"
[ -r "$scn" ] || { echo "lr3_capture: no such scenario: $scn" >&2; exit 2; }
mkdir -p docs/captures/lr3 docs/metrics
vcd="docs/captures/lr3/$name.vcd"
serial="docs/captures/lr3/$name.serial.log"
report="docs/metrics/analyzer-$name.txt"
rm -f "$vcd" "$serial" "$report"

log="$(mktemp)"
rc=0
wokwi-cli --timeout "$timeout_ms" --scenario "$scn" --diagram-file scenarios/lr3.diagram.json \
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

python3 tools/lr3_analyze.py "$vcd" "$mode" --serial "$serial" | tee "$report"