# Self-test and console output format

The firmware talks over USART2 (PA2/PA3, 115200 8N1, ST-LINK virtual COM port). Every
message is one line of ASCII ending in `\r\n`. The lines are written to be read by a
person and parsed by a script: `scripts/check_selftest.sh` checks a serial log against this
page, and `tests/test_app.c` compares the firmware output with the same strings.

## 1. Start-up report

After reset the firmware prints a banner, then the self-test report:

```
Heliostat boot OK
SELFTEST BEGIN version=1
SELFTEST adc OK raw=1025,2050,3074,4095
SELFTEST spi OK
SELFTEST sd OK type=V2HC sig=55AA
SELFTEST servo1 OK pulse_us=1500
SELFTEST servo2 OK pulse_us=1500
SELFTEST END total=5 passed=5 failed=0
```

Rules:

1. `SELFTEST BEGIN version=1` opens the report, `SELFTEST END ...` closes it. The format
   version is bumped when a line changes.
2. Exactly five check lines in a fixed order: `adc`, `spi`, `sd`, `servo1`, `servo2`.
   A failed check never stops the later ones, so one report shows every problem.
3. `END` repeats the arithmetic: `total=5`, `passed` is the number of `OK` lines and
   `failed` the number of `FAIL` lines. A parser must reject an `END` line that disagrees
   with the lines above it, because that means the report was damaged in transit.
4. Lines that do not start with `SELFTEST` (the banner, anything else) are ignored by the parser.

### Check lines

| Check | OK line | FAIL line(s) |
|---|---|---|
| adc | `SELFTEST adc OK raw=<a>,<b>,<c>,<d>` four raw values 0..4095, channel order PA0, PA1, PB1, PB2 | `SELFTEST adc FAIL status=<s> at=<n>` where `at` is the channel index that failed, or `-1` if the ADC calibration failed before any conversion |
| spi | `SELFTEST spi OK` | `SELFTEST spi FAIL status=<s>` |
| sd | `SELFTEST sd OK type=<V1\|V2\|V2HC> sig=<XXXX>` where `sig` is bytes 510 and 511 of block 0 in upper-case hex (`55AA` is valid) | `SELFTEST sd FAIL step=init status=<s>`, `SELFTEST sd FAIL step=read status=<s>` or `SELFTEST sd FAIL step=signature sig=<XXXX>`. If SPI failed, the SD check fails at `step=init` without touching the bus |
| servo1, servo2 | `SELFTEST servoN OK pulse_us=<n>` after commanding neutral (90 degrees, 1500 us) | `SELFTEST servoN FAIL status=<s>`. If the PWM timer did not start, both servos fail |

### Status codes (`status=`)

| Value | Name | Meaning |
|---|---|---|
| 0 | PLAT_OK | success (never printed on a FAIL line) |
| 1 | PLAT_ERR_ARG | invalid argument |
| 2 | PLAT_ERR_TIMEOUT | no answer in time (for example no card in the slot) |
| 3 | PLAT_ERR_BUS | bus or peripheral error (for example the card returned an error token) |

## 2. Console commands

After the report the firmware reads commands from the same port. A command is one line
terminated by CR or LF; empty lines are ignored; tabs and spaces separate arguments; the
line is at most 48 characters. Every command answers with exactly one `CMD ...` line,
except `selftest`, which prints the report first.

| Command | Reply on success | Reply on failure |
|---|---|---|
| `help` | `CMD help OK commands=help,selftest,adc,servo,sd,time,track,deadband,zero,stow,resume,button` | none |
| `selftest` | the report from section 1, then `CMD selftest OK failed=0` | the report, then `CMD selftest FAIL failed=<n>` |
| `adc` | `CMD adc OK raw=<a>,<b>,<c>,<d>` | `CMD adc FAIL status=<s> at=<n>` |
| `servo <1\|2> <0..180>` | `CMD servo OK ch=<c> angle=<deg> pulse_us=<us>` (1000 us at 0, 2000 us at 180) | `CMD servo FAIL reason=usage`, `reason=range` (angle above 180) or `status=<s>` |
| `sd init` | `CMD sd-init OK type=<T>` (runs the whole init sequence again) | `CMD sd-init FAIL status=<s>` |
| `sd read <block>` | `CMD sd-read OK block=<n> sig=<XXXX> first16=<32 hex digits>` | `CMD sd-read FAIL reason=usage`, or `step=init` / `step=read` with `status=<s>` |
| `sd write <block>` | `CMD sd-write OK block=<n> us=<t>`: writes 512 bytes, byte *i* = (*i* + block) mod 256; `us` is the time of the write | `CMD sd-write FAIL reason=usage`, `reason=protected` (block below 8192), or `step=init` / `step=write` with `status=<s>` |
| `sd` with a missing or unknown subcommand | none | `CMD sd FAIL reason=usage` |
| `time` | `CMD time OK hz=<f> read_cycles=<n> backsteps=<k>`: 20000 pairs of back-to-back readings of the time base; `read_cycles` is the smallest cost of one reading in cycles, `backsteps` counts pairs where the second reading was earlier | none |
| `track` | `CMD track OK az=<a> tilt=<t> band=<d> zero=<a>,<t> parked=<0\|1> stowed=<0\|1> dark=<n> runs=<n> missed=<n> errors=<n>`: current angles, dead zone, safe position, dark samples counted so far, runs of the 50 ms tracking task, releases skipped because the loop was late, runs that hit an ADC or servo error | none |
| `deadband <0..4095>` | `CMD deadband OK value=<n>` | `CMD deadband FAIL reason=usage` or `reason=range` |
| `zero <az> <tilt>` | `CMD zero OK az=<a> tilt=<t>`: sets the safe position (0..180 degrees each); a parked or stowed panel moves there at once | `CMD zero FAIL reason=usage`, `reason=range` or `status=<s>` |
| `stow` | `CMD stow OK az=<a> tilt=<t>`: moves to the safe position and holds it | `CMD stow FAIL status=<s>` |
| `resume` | `CMD resume OK`: tracking continues | none |
| `button` | `CMD button OK edges=<n> accepted=<n> ignored=<n>`: falling edges seen on the pushbutton pin, presses that stowed the panel, edges that were ignored (inside the 200 ms lockout or part of the same press) | `CMD button FAIL status=<s>` if the pin could not be started |
| anything else | none | `CMD <word> FAIL reason=unknown` |
| a line longer than 48 characters | none | `CMD ? FAIL reason=too-long` |

Besides replies the firmware prints event lines on its own. `EVT stow t_us=<t> lat_us=<l>` follows a pushbutton press (falling edge on PB5) that has stowed the panel: `t_us` is the time of the edge in microseconds on the free-running time base (a 32-bit cycle counter that wraps every 357.9 s), `lat_us` the time from the edge until both servo outputs were written. `EVT stow FAIL status=<s>` means a servo write failed. A press less than 200 ms after the last accepted one is ignored and only counted.

## 3. How it is checked

The tracking task starts after the self-test and runs every 50 ms without printing anything. A `servo` command sets the position from which tracking continues; `stow` holds the panel at the safe position until `resume`.

- Host tests (`make check`): `tests/test_app.c` runs the firmware logic against a fake UART and
  compares the output with the strings above, including every failure line.
- Simulator (`make flash`): the run succeeds only if the line
  `SELFTEST END total=5 passed=5 failed=0` appears within the timeout, so a failing
  check makes the target fail. The serial output is saved to `docs/screenshots/last-run-serial.log`.
- Log check: `scripts/check_selftest.sh docs/screenshots/last-run-serial.log` validates every
  line against section 1 and exits 0 (all passed), 1 (a check failed) or 2 (report missing or
  malformed). If a log holds several reports (the console command `selftest` prints another
  one), only the first is checked.
