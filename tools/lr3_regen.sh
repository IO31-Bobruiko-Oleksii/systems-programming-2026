#!/usr/bin/env bash
# Rebuilds every report in docs/metrics from the committed serial logs and captures, with the
# tools of the working tree. No simulator, no Docker: it runs in a few seconds.
#
# usage: tools/lr3_regen.sh        then look at what changed: git diff --stat docs/metrics
#
# What it does not touch: docs/metrics/memory.txt (needs Docker: scripts/lr3_memory.sh) and
# docs/metrics/soak.txt (tests/soak.sh). The serial logs and captures are never rewritten.
set -euo pipefail
cd "$(dirname "$0")/.."
export PYTHONDONTWRITEBYTECODE=1

cap=docs/captures/lr3
met=docs/metrics

for n in s1-idle s2-nominal s3-worst e1-idle nosd-log; do
    python3 tools/lr3_stats.py "$cap/$n.serial.log" --scenario "scenarios/$n.yaml" > "$met/stats-$n.txt"
done
python3 tools/lr3_park.py "$cap/park.serial.log" --scenario scenarios/park.yaml > "$met/stats-park.txt"

python3 tools/lr3_analyze.py "$cap/stats.vcd" loop --serial "$cap/stats.serial.log" > "$met/analyzer-stats.txt"
python3 tools/lr3_analyze.py "$cap/load.vcd" loop --serial "$cap/load.serial.log" > "$met/analyzer-load.txt"
python3 tools/lr3_analyze.py "$cap/stow.vcd" event --serial "$cap/stow.serial.log" > "$met/analyzer-stow.txt"
python3 tools/lr3_analyze.py "$cap/stow-busy.vcd" event --serial "$cap/stow-busy.serial.log" > "$met/analyzer-stow-busy.txt"
python3 tools/lr3_burst.py "$cap/burst.vcd" --serial "$cap/burst.serial.log" > "$met/analyzer-burst.txt"
for n in sweep-uart-poll sweep-uart sweep-ms; do
    python3 tools/lr3_sweep.py "$cap/$n.vcd" --serial "$cap/$n.serial.log" > "$met/analyzer-$n.txt"
done
python3 tools/lr3_sweep.py "$cap/flood.vcd" --serial "$cap/flood.serial.log" \
    --names 1=stats-reply,2=sd-write > "$met/analyzer-flood.txt"
python3 tools/lr3_startup.py "$cap/burst.vcd" > "$met/startup.txt"

echo "lr3_regen: reports rebuilt in $met"
