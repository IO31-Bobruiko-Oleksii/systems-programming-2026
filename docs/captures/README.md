# Logic-analyzer captures

Six value-change-dump (VCD) files recorded from the Wokwi simulation of the heliostat product
(NUCLEO-C031C6, STM32C031C6) with the `wokwi-logic-analyzer` part `logic1` wired to eight pins.
They are the evidence for [analysis.md](analysis.md) and the source of the test vectors in
`tests/vectors/` (see "Replay vectors" below).

## Files

| File | Content | Made by |
|------|---------|---------|
| `NAME.vcd` | eight-channel capture of one run | `scripts/capture.sh` |
| `NAME.serial.log` | serial log of the same run | `scripts/capture.sh` |
| `NAME.decoded.txt` | text decode and timing measurements | `scripts/decode_captures.sh` |
| `analysis.md` | the analysis of all six | written by hand from the numbers in the `.decoded.txt` files |

`NAME.cli.log` files are written as well and are not committed.

## Channel plan (same in every capture)

| Channel | Signal | MCU pin | Used for |
|---------|--------|---------|----------|
| D0 | SPI1 SCK | PA5 | SD card clock |
| D1 | SPI1 MOSI | PA7 | commands and data to the card |
| D2 | SPI1 MISO | PA6 | responses and data from the card |
| D3 | SD chip select (software, active low) | PB9 | frames a transaction |
| D4 | USART2 TX | PA2 | self-test report and console replies, 115200 8N1 |
| D5 | TIM3_CH1 PWM | PC6 | servo 1 |
| D6 | TIM3_CH2 PWM | PC7 | servo 2 |
| D7 | service marker | PB3 | high while the firmware reads one ADC channel |

I2C, SCCB, the camera bus and DMA do not exist in this product, so there is no capture for them.
The ADC has no digital traffic: the marker on D7 (set and cleared by the firmware around each
conversion, see `firmware/platform/src/plat_adc_hal.c`) is the way to see when a measurement
happens and how often.

## Capture parameters (the same for all six)

| Parameter | Value |
|-----------|-------|
| Source | `wokwi-cli --vcd-file` (wokwi-cli 0.27.1), part `logic1` in `diagram.json` |
| Time base | VCD `$timescale 1 ns`; every timestamp is a multiple of 25 ns, so the resolution is 25 ns (equivalent to 40 MHz sampling) |
| Sample rate needed | highest signal is SCK at 6 MHz: 40 MHz gives 6.7 samples per SCK period; a UART bit (8.67 us) is 347 time steps long (35 after the import factor 250 below) |
| Trigger | none (`triggerMode` is `off` in `diagram.json`): the capture runs from the start of the simulation (t = 0, reset) to the end of the scenario, so every event can be found by its time since reset |
| Buffer | `bufferSize` 1000000 in `diagram.json` |
| Levels | ideal 0 / 1; no rise times, no analog values |
| Ground | not applicable in the simulator (see section 9 of analysis.md) |

## The captures

All captures except `nosd` start with the same 45 ms: banner, the five self-test lines
(`adc`, `spi`, `sd`, `servo1`, `servo2`), the SD card initialisation and one block read. The
extra activity of each capture is listed below.

| Capture | Duration | What the firmware does after the self-test | What to look at |
|---------|----------|---------------------------------------------|-----------------|
| `boot` | 105.228 ms | nothing, the self-test only | reference capture: all five SD commands, block-0 read, ADC burst, servo PWM start |
| `console` | 66.410 ms | console: `help`, an unknown command (`foo`), a line over the 48-character limit | UART replies `CMD help OK`, `CMD foo FAIL reason=unknown`, `CMD ? FAIL reason=too-long` |
| `adc` | 60.293 ms | console command `adc` | second burst of four marker pulses on D7, then the reply line |
| `servo` | 242.180 ms | console commands `servo 1 0`, `servo 1 180`, `servo 2 45` | PWM widths 1000, 2000 us on D5 and 1250 us on D6, period 20 ms |
| `sdread` | 71.799 ms | console command `sd read 0` | a second, complete CMD17 transaction (window 3) with CS held low |
| `nosd` | 52.187 ms | the SD card is absent | deliberate anomaly: MISO stays low, CMD0 is sent five times, `SELFTEST sd FAIL step=init status=3` |

## Reproduce

```bash
scripts/capture.sh all        # records all six (needs wokwi-cli 0.27.1 and the firmware built by make build)
scripts/capture.sh servo      # one capture; the name is the one in the table above
make capture NAME=servo       # the same through the root Makefile
```

The stimulus for each capture is a firmware command or a boot-time action, so the same transaction
can be provoked again (see `scenarios/`).

## Decode

```bash
scripts/decode_captures.sh                      # writes docs/captures/*.decoded.txt, needs python3 >= 3.10
sudo apt install sigrok-cli                     # optional: adds the sigrok sections and the cross-check
python3 scripts/vcd_tool.py report docs/captures/boot.vcd
python3 scripts/vcd_tool.py crosscheck docs/captures/boot.vcd
```

`scripts/vcd_tool.py` uses only the Python standard library. With sigrok-cli installed the script
compares its UART and SPI byte streams with the `uart` and `spi` decoders of libsigrokdecode;
on all six captures they are identical (the result is the last lines of each `.decoded.txt`).
In each `.decoded.txt` the numbers in front of the sigrok lines are sample numbers
(1 sample = 250 ns for UART, 25 ns for SPI). The first SPI line, `0-153893 spi-1:` with no bytes, is
CS held low from reset until 3.847 ms, with no clock.

## Open the captures in PulseView

1. File, Import, "Import Value Change Dump data". Number of channels 8. Downsampling factor 25 for
   SPI (40 MHz), 250 for UART, PWM and the ADC marker (4 MHz). Other fields stay at the defaults.
   Do not import at factor 1: 1 ns resolution is 105 million samples for the boot capture and
   freezes the virtual machine.
2. UART decoder: TX = D4, baud rate 115200, 8 data bits, no parity, 1 stop bit, LSB first.
3. SPI decoder: CLK = D0, MOSI = D1, MISO = D2, CS# = D3, CS active low, CPOL 0, CPHA 0, MSB first,
   8 bits per word. The "SD card (SPI mode)" decoder can be stacked on top of it
   (it gets confused by `nosd`, where MISO never answers).
4. PWM decoder or cursors on D5 and D6; cursors on D7 for the marker width.

## Replay vectors

`tests/test_sd_replay.c` feeds the SD driver (`firmware/devices/src/sdcard.c`) with the bytes seen on
the wires and checks that it reaches the same result as the firmware reported on the UART in the same
run. The byte arrays are generated from the captures and not typed by hand:

```bash
python3 scripts/vcd_tool.py vectors docs/captures/boot.vcd --name SD_BOOT --out tests/vectors/sd_boot.inc
python3 scripts/vcd_tool.py vectors docs/captures/nosd.vcd --name SD_NOSD --out tests/vectors/sd_nosd.inc
```

The first comment of each `.inc` file holds the name and the SHA-256 of the capture it came from.
