#!/usr/bin/env bash
# lr4_os.sh <scenario> <build-dir> [timeout_ms]
set -u
cd "$(dirname "$0")/.."

scn="${1:?usage: $0 <scenario> <build-dir> [timeout_ms]}"
dir="${2:?usage: $0 <scenario> <build-dir> [timeout_ms]}"
timeout_ms="${3:-60000}"
elf="firmware/build/$dir/firmware.elf"
out="docs/captures/lr4/os"
serial="$out/$scn.$dir.serial.log"

[ -r "$elf" ] || { echo "lr4_os: no such firmware: $elf" >&2; exit 2; }
[ -r "scenarios/$scn.yaml" ] || { echo "lr4_os: no such scenario: scenarios/$scn.yaml" >&2; exit 2; }
mkdir -p "$out"
rm -f "$serial"
echo "lr4_os $scn: elf=$elf sha256=$(sha256sum "$elf" | cut -c1-12)"
wokwi-cli --timeout "$timeout_ms" --scenario "scenarios/$scn.yaml" \
    --diagram-file scenarios/lr3.diagram.json --elf "$elf" \
    --serial-log-file "$serial" > "$out/$scn.$dir.cli.log" 2>&1
echo "lr4_os $scn: wokwi-cli rc=$? (a scenario that ends in a halted firmware may end with a timeout)"
tr -d '\r' < "$serial" | grep -a -E 'RTOS up|CMD (inv|qflood|starve|tasks|heap|load|log)|STAT (task|os|name=(late|period|track)|count)|EVT|FATAL|SELFTEST END' | cut -c1-170