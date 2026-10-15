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
| `help` | `CMD help OK commands=help,selftest,adc,servo,sd` | none |
| `selftest` | the report from section 1, then `CMD selftest OK failed=0` | the report, then `CMD selftest FAIL failed=<n>` |
| `adc` | `CMD adc OK raw=<a>,<b>,<c>,<d>` | `CMD adc FAIL status=<s> at=<n>` |
| `servo <1\|2> <0..180>` | `CMD servo OK ch=<c> angle=<deg> pulse_us=<us>` (1000 us at 0, 2000 us at 180) | `CMD servo FAIL reason=usage`, `reason=range` (angle above 180) or `status=<s>` |
| `sd init` | `CMD sd-init OK type=<T>` (runs the whole init sequence again) | `CMD sd-init FAIL status=<s>` |
| `sd read <block>` | `CMD sd-read OK block=<n> sig=<XXXX> first16=<32 hex digits>` | `CMD sd-read FAIL reason=usage`, or `step=init` / `step=read` with `status=<s>` |
| `sd` with a missing or unknown subcommand | none | `CMD sd FAIL reason=usage` |
| anything else | none | `CMD <word> FAIL reason=unknown` |
| a line longer than 48 characters | none | `CMD ? FAIL reason=too-long` |

## 3. How it is checked

- Host tests (`make check`): `tests/test_app.c` runs the firmware logic against a fake UART and
  compares the output with the strings above, including every failure line.
- Simulator (`make flash`): the run succeeds only if the line
  `SELFTEST END total=5 passed=5 failed=0` appears within the timeout, so a failing
  check makes the target fail. The serial output is saved to `docs/screenshots/last-run-serial.log`.
- Log check: `scripts/check_selftest.sh docs/screenshots/last-run-serial.log` validates every
  line against section 1 and exits 0 (all passed), 1 (a check failed) or 2 (report missing or
  malformed). If a log holds several reports (the console command `selftest` prints another
  one), only the first is checked.
