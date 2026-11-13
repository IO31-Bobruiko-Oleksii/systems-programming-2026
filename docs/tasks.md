# Tasks of the product

The PRD lists functional requirements but gives no numbers for time. This file turns every
requirement into a task with a trigger, a period and a deadline. The numbers are engineering
assumptions, each with its reason below. Measurements in LR3 are judged against these deadlines
using the **maximum** observed value, not the average. All times are simulator times (Wokwi,
core clock 12 MHz), see the report for what that means.

## Tasks

| ID | Task | Requirements | Trigger | Period / interval | Deadline |
|---|---|---|---|---|---|
| T1 | Sense: read the 4 light channels | FR-01 | time | 50 ms | finished within 5 ms of its start |
| T2 | Track: differences of opposite pairs, dead zone, move the servos | FR-02, FR-03, FR-04 | after T1 | 50 ms | servo pulse width updated within 25 ms after the nominal release of T1 |
| T3 | Park: all four channels dark for 20 samples in a row, then move to the safe position | FR-06 | inside T2 | 50 ms | park command at most 1.1 s after the light disappears |
| T4 | Console: receive and execute a command line | FR-07 | UART byte | one byte per 86.8 us at 115200 8N1 | no byte lost; reply within 100 ms after the line terminator |
| T5 | Log: store a record when a servo moved or the panel parked or stowed, write 512-byte blocks to the SD card from block 8192 | FR-05 | tracking step that moved a servo, accepted stow press | at most one record per 50 ms (plus one per stow press) | a block is written within 10 s after its first record; a write blocks the loop for about 13 ms |
| T6 | Self-test of ADC, PWM, SPI, UART | FR-08 | once after reset | once | main loop running within 1 s after reset |
| E1 | Stow request: pushbutton drives the panel to the safe position (extension of FR-06) | FR-06 | falling edge on the button pin | at least 200 ms between requests | servo pulse width updated within 10 ms after the edge; pulse on the servo output changes within 30 ms after the edge |

The pushbutton is not in the PRD. It is added so that the system has an external event with a
real deadline (think of a wind alarm that must stow the panel). The pin is chosen in
`docs/pinout.md` when the button is added.

## Reasons for the numbers

- **50 ms (T1, T2):** the photoresistor needs about 20-45 ms to respond (`docs/hardware-components.md`),
  so sampling faster gains nothing and slower would miss changes. It is also several times faster
  than the servo can move (0.10 s per 60 degrees).
- **25 ms (T2):** half a period. The other half is left for the console, the logger and the
  measurement code in the same loop pass.
- **20 dark samples, 1.1 s (T3):** a passing cloud shorter than 1 s must not park the panel.
  1.1 s is the 1 s plus two periods of slack.
- **100 ms, no lost byte (T4):** a reply within 100 ms feels instant. A lost byte silently
  corrupts a calibration command, and the UART has only a one-byte hardware buffer.
- **10 s (T5):** the log is not time-critical, but it bounds how much is lost if power fails.
  A record is 32 bytes, so a block holds 16 records.
- **1 s (T6):** the product should be usable within a second of power-up.
- **10 ms and 30 ms (E1):** 10 ms is half of a 20 ms PWM frame, so software is never the
  dominant part. 30 ms adds one frame, because a new pulse width reaches the servo at the next
  frame at the earliest (assumption, checked on the logic analyzer).

## Scenarios

The three load scenarios of LR3 (section 6.1), as they were run. The commands, the real time and
the other scenarios of section 6.2 are in `docs/baseline-lab4.md`, section 4.

- **S1 idle:** potentiometers constant, no console commands, no button presses. One window of
  250 s, 5000 T2 runs.
- **S2 nominal:** ticks of 4 s (32 ticks). At every tick potentiometer 2 steps through 0.9, 0.5,
  0.1, 0.5 so that tracking works, and one write of four console commands goes out (one command
  per second on average); logging is on. At every eighth tick there is one button press followed
  by `resume`: 4 presses, 32 s apart. One window of 129 s, about 2580 T2 runs.
- **S3 worst:** the ticks of S2, 9 of them. In each tick the four commands, then one of: an
  `sd write` alone, a press at a random moment, a burst of 20 `track` lines in one write; after
  that three presses during an `sd write`. 30 presses in all, 27 of them during an SD write. One
  window of 47 s, 946 T2 runs.

Periodic tasks are judged over thousands of runs where the length of the scenario allows it: S1
has 5000 runs and S2 about 2580. S3 has 946: it was kept short to fit one Wokwi session, because
every scenario step costs real time and the session cap of the plan used for the first runs was
5 minutes. Events are judged over at least 30
repetitions: E1 has 30 presses in S3 and 40 in the idle scenario `e1-idle`. The park test
(T3) has 16 cycles, so the verdict on T3 rests on 16 repetitions.

### Functional scenarios

These show the status of each requirement in `docs/PRD.md`, section D. They are short and wait for
fixed replies; they measure nothing. `boot`, `adc`, `servo`, `console` and `sdread` (LR2) run on the
root `diagram.json`, `nosd` on `scenarios/nosd.diagram.json`, the others on
`scenarios/lr3.diagram.json`.

| Scenario (`scenarios/`) | What it checks | Requirement |
|---|---|---|
| `boot.yaml` | the start-up self-test, every bus once | FR-08 |
| `nosd.yaml` | the self-test with the card's MISO wire cut (`nosd.diagram.json`) reports `passed=4 failed=1` | FR-08 |
| `adc.yaml` | one console conversion of the four channels against the known pot positions | FR-01 |
| `servo.yaml` | pulse width at 0, 180 and 45 degrees | FR-03 |
| `track.yaml` | the panel follows the light to the end stops; `stow`, `resume`; the night park | FR-02, FR-03, FR-06 |
| `calibrate.yaml` | the dead zone and the zero position set over UART, values out of range refused, the park at the new zero | FR-04, FR-06, FR-07 |
| `park.yaml` | 16 cycles of light out and back, with a cloud shorter than 1 s: park time against 1.1 s | FR-06 (T3) |
| `stow.yaml` | the button stows the panel, a second press inside 200 ms is ignored, a later one is accepted | E1 |
| `console.yaml` | `help`, an unknown command, an over-long line | FR-07 |
| `sdread.yaml` | a single-block read of block 0 over SPI at 6 MHz | FR-05 |
| `log.yaml` | tracking moves are stored in 32-byte records and read back | FR-05 |
