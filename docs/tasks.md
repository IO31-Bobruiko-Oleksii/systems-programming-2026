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
| T5 | Log: store a record when a servo moved, write full 512-byte blocks to the SD card | FR-05 | tracking step that moved a servo | at most one record per 50 ms | a block is written within 10 s after its first record |
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

- **S1 idle:** potentiometers constant, no console commands, no button presses.
- **S2 nominal:** one potentiometer step every 2 s so tracking works, one console command per
  second, logging on, one button press every 30 s.
- **S3 worst:** S2 plus repeated `sd write` and `sd read`, bursts of 20 console commands sent
  without waiting, and 30-50 button presses at random moments, many of them during an SD write.

Periodic tasks are judged over thousands of runs (5000 periods is 250 s of simulated time),
events over at least 30 repetitions.