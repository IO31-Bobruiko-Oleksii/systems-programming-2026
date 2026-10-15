#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

name="${1:?usage: $0 <scenario name | all>}"

if [ "$name" = all ]; then
    failed=0
    for f in scenarios/*.yaml; do
        "$0" "$(basename "$f" .yaml)" || failed=$((failed + 1))
    done
    [ "$failed" -eq 0 ] || { echo "capture: $failed scenario(s) failed"; exit 1; }
    exit 0
fi

scn="scenarios/$name.yaml"
[ -r "$scn" ] || { echo "capture: no such scenario: $scn" >&2; exit 2; }
mkdir -p docs/captures
vcd="docs/captures/$name.vcd"
serial="docs/captures/$name.serial.log"
cli="docs/captures/$name.cli.log"
rm -f "$vcd" "$serial" "$cli"

opts=(--timeout 5000 --scenario "$scn" --vcd-file "$vcd" --serial-log-file "$serial")
if [ -r "scenarios/$name.diagram.json" ]; then
    opts+=(--diagram-file "scenarios/$name.diagram.json")
fi

rc=0
wokwi-cli "${opts[@]}" 2>&1 | tee "$cli" || rc=$?

ok=1
[ "$rc" -eq 0 ] || ok=0
grep -q "Scenario completed successfully" "$cli" || ok=0
[ -s "$vcd" ] || ok=0

if [ -s "$vcd" ]; then
    stamp=$(awk '/\$timescale/ { f = 1 } f { printf "%s ", $0 } f && /\$end/ { exit }' "$vcd")
    last=$(grep -E '^#[0-9]+$' "$vcd" | tail -1 || true)
    size=$(stat -c %s "$vcd")
else
    stamp="-"; last="-"; size=0
fi
lines=0
[ -f "$serial" ] && lines=$(wc -l < "$serial")

if [ "$ok" -eq 1 ]; then verdict=OK; else verdict=FAILED; fi
echo "capture $name: $verdict rc=$rc vcd=${size}B timescale=[$stamp] last=$last serial_lines=$lines"
[ "$ok" -eq 1 ]
