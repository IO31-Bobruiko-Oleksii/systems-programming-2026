#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

name="${1:?usage: $0 <scenario> [timeout_ms]}"
timeout_ms="${2:-600000}"

scn="scenarios/$name.yaml"
[ -r "$scn" ] || { echo "lr3_run: no such scenario: $scn" >&2; exit 2; }
mkdir -p docs/captures/lr3 docs/metrics
diagram="scenarios/lr3.diagram.json"
[ -r "scenarios/$name.diagram.json" ] && diagram="scenarios/$name.diagram.json"
serial="docs/captures/lr3/$name.serial.log"
report="docs/metrics/stats-$name.txt"
rm -f "$serial" "$report"

log="$(mktemp)"
rc=0
t0=$(date +%s)
T0=$(date +%s%3N)
stamp() {
    local line ms
    while IFS= read -r line; do
        ms=$(( $(date +%s%3N) - T0 ))
        printf '%4d.%03d %s\n' $((ms / 1000)) $((ms % 1000)) "$line"
    done
}
wokwi-cli --timeout "$timeout_ms" --scenario "$scn" --diagram-file "$diagram" \
    --serial-log-file "$serial" 2>&1 | stamp | tee "$log" || rc=$?

t1=$(date +%s)
echo "lr3_run $name: $((t1 - t0)) s of real time"

ok=1
grep -q "Scenario completed successfully" "$log" || ok=0
[ -s "$serial" ] || ok=0
if [ "$ok" -eq 1 ] && [ "$rc" -ne 0 ]; then
    echo "lr3_run $name: the scenario completed, the CLI then ended with rc=$rc (session limit?)"
fi
mv "$log" "docs/captures/lr3/$name.cli.log"
if [ "$ok" -ne 1 ]; then
    echo "lr3_run $name: simulator run FAILED (rc=$rc)"
    exit 1
fi

python3 tools/lr3_stats.py "$serial" --scenario "$scn" | tee "$report"
