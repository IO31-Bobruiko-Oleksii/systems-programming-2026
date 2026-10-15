#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/.." || exit 1

tool=scripts/vcd_tool.py
if [ "$#" -gt 0 ]; then
    files=("$@")
else
    files=(docs/captures/*.vcd)
fi
if [ ! -e "${files[0]}" ]; then
    echo "no .vcd files found (looked in docs/captures)" >&2
    exit 1
fi

have_sigrok=0
if command -v sigrok-cli >/dev/null 2>&1; then
    have_sigrok=1
else
    echo "note: sigrok-cli is not installed (sudo apt install sigrok-cli); the sigrok sections are skipped"
fi

spi="spi:clk=D0:mosi=D1:miso=D2:cs=D3"
status=0
for vcd in "${files[@]}"; do
    out="${vcd%.vcd}.decoded.txt"
    tmp="$out.tmp"
    if ! python3 "$tool" report "$vcd" > "$tmp"; then
        echo "FAILED: $vcd" >&2
        rm -f "$tmp"
        status=1
        continue
    fi
    if [ "$have_sigrok" = 1 ]; then
        {
            echo
            echo "=== sigrok-cli: UART on D4, 115200 8N1, one line per packet ending in LF"
            echo "=== numbers = first sample .. last sample, 1 sample = 250 ns (vcd downsample 250)"
            sigrok-cli -I vcd:downsample=250 -i "$vcd" \
                -P uart:tx=D4:baudrate=115200:format=ascii:tx_packet_delim=10 \
                -A uart=tx-packet --protocol-decoder-samplenum 2>/dev/null
            echo
            echo "=== sigrok-cli: SPI mode 0 (D0 SCK, D1 MOSI, D2 MISO, D3 CS low), one line per CS-low transfer"
            echo "=== numbers = first sample .. last sample, 1 sample = 25 ns (vcd downsample 25)"
            echo "--- MOSI"
            sigrok-cli -I vcd:downsample=25 -i "$vcd" -P "$spi" \
                -A spi=mosi-transfer --protocol-decoder-samplenum 2>/dev/null
            echo "--- MISO"
            sigrok-cli -I vcd:downsample=25 -i "$vcd" -P "$spi" \
                -A spi=miso-transfer --protocol-decoder-samplenum 2>/dev/null
        } >> "$tmp"
        echo >> "$tmp"
        echo "=== cross-check: scripts/vcd_tool.py against sigrok-cli (byte streams)" >> "$tmp"
        xc=$(python3 "$tool" crosscheck "$vcd")
        rc=$?
        printf '%s\n' "$xc" >> "$tmp"
        printf '%s\n' "$xc" | sed 's/^/    /'
        if [ "$rc" -ne 0 ]; then
            status=1
        fi
    fi
    mv "$tmp" "$out"
    echo "wrote $out"
done
exit "$status"
