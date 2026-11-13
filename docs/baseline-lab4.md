# Baseline for LR4

This file fixes what LR4 repeats on FreeRTOS: the scenarios with exact reproduction steps, the
final metrics of the super-loop firmware, the hypotheses from `docs/limitations.md`, and the
measurement instrumentation that LR4 uses without change. Judging rules are those of
`docs/tasks.md`: every deadline is compared with the **maximum**, not the average. All times are
simulator times (Wokwi, 12 MHz core clock).

Contents: 1 rules for LR4, 2 which build produced which number, 3 environment, 4 scenarios,
5 final metrics, 6 hypotheses, 7 instrumentation, 8 carrying it to FreeRTOS, 9 checklist,
10 known gaps.

## 1. Rules for LR4

LR4 is a comparison, so everything except the firmware architecture stays as it is here.

| Stays unchanged | Where |
|---|---|
| scenario files and the seed of the generator (2026) | `scenarios/*.yaml`, `tools/lr3_gen.py` |
| the analyzer wiring and the service pins | `scenarios/lr3.diagram.json`, `burst.diagram.json`, `nosd-log.diagram.json` |
| host scripts and the way they read the logs | `tools/lr3_*.py`, `tools/lr3_*.sh` |
| the console commands and the `STAT` / `EVT` / `CMD` line formats | section 7 |
| the definition of every metric | section 7.3 |
| the compiler flags (`-Og`, arm-none-eabi-gcc 10.3.1 in the `stm32-build` image) | `firmware/Makefile`, `docker/Dockerfile` |
| the simulated board, core clock 12 MHz, UART 115200 8N1 | `wokwi.toml`, `diagram.json` |

What may change: the firmware (`firmware/`), the host-side additions that LR4 needs (new files
under `tools/`, never an edit that changes an LR3 result), and the numbers in the LR4 report.

If a script or a scenario has to change, change it in a new commit that says so, add a line to
the change log at the end of this file, and re-run the LR3 scenarios with it, so that the two
columns of the comparison were made with the same tool.

Comparison rules (LR3 section 6.3): against the PRD by the maximum; between scenarios as S3
minus S1; against a calculation, explaining any difference over 10..20 %; between the two
measurement methods (counter in the firmware and logic analyzer) for at least one metric.

## 2. Which build produced which number

LR3 asks that every scenario run on one build, identified by a git hash in the banner and the logs.
The banner of this firmware (`Heliostat boot OK`) has no hash, and the firmware changed while the
measurements were being made. The build of each result is therefore reconstructed from the commit
in which the capture was committed. The firmware files changed only in these commits
(`git log -- firmware`):

| Build | Last firmware commit | What it added | Results taken on it |
|---|---|---|---|
| A | `a5b6b65` | the measurement code and the probe pins | `stats`, `stow`, `stow-busy` captures |
| B | `9ede332` | `load` command (9476bf8), restart of the T2 grid in `load` | `load`, `sweep-ms`, `sweep-uart-poll` captures |
| C | `42f6691` | interrupt-driven receive ring of 256 bytes | `sweep-uart`, `flood`, S1, S2, S3 |
| D | `19b0119` | the stow lockout keeps the interrupt time stamp | e1-idle, burst, nosd-log, park, start-up, memory, soak |

D is the firmware at the head of the branch (no firmware commit after `19b0119`). The tables in
section 5 carry the build letter next to each result.

What the differences mean for the numbers:

- A and B have no receive ring. Their `P` and loop-pass figures lack the interrupt handler
  (about 13 us per byte received during a pass), and the reply of `stats` lacks the `STAT rx`
  line, which adds about 7 ms of transmission. The pass of the `stats` reply, 82.7 ms on A, is
  therefore expected near 90 ms on D (the `cmd` maximum of the same reply was 86.9 ms on C); this
  has to be read from a re-run. Section 5 prints the A and B figures as they are; the hypotheses
  that cite them (stow-busy 31.4 ms, `sweep-ms` break points) are baselines of the old build until
  re-run.
- C to D changed only the handling of a second press inside the 200 ms lockout. It does not touch
  S1 and the sweeps. For S2 and S3 it changes how an extra press is counted (S2 shows 5 accepted
  for 4 presses on C; the e1-idle run on D shows 40 of 40), not the timing of tasks.
- The memory figures, the start-up milestones, the host soak and the park, nosd-log and e1-idle runs
  are D.

**Re-run plan** (optional but recommended before LR4 starts; the real time is estimated from the
cost model in section 3):

| Group | Scenarios | Real time | Why |
|---|---|---|---|
| 1 (A and B captures on D) | `tools/lr3_capture.sh stats loop`, `stow event`, `stow-busy event`, `load loop`, `sweep-ms sweep 60000` | about 3.5 min in all | gives every analyzer number on the final build |
| 2 (C results on D) | `tools/lr3_run.sh s2-nominal 600000`, `s3-worst 600000`, then `sweep-uart`, `flood` | about 8 min for S2 and S3, 2.5 min for the other two | S2 and S3 contain presses; S1 does not and is not affected |

After a re-run, refresh the numbers in sections 5 and 6 and in the evidence of
`docs/limitations.md` that cite them, and delete the build letters that no longer differ. Until
then the letters are the honest label.

How to identify a run from now on: write `git rev-parse --short HEAD` and
`sha256sum firmware/build/firmware.elf` into the commit message of every set of captures, and
never commit captures taken from a dirty `firmware/` tree. For LR4 the same rule applies with the
FreeRTOS build, so the two columns of the comparison name their builds.

## 3. Environment

| Item | Value |
|---|---|
| Board, simulator | NUCLEO-C031C6 (Cortex-M0+, 12 MHz in the simulator), Wokwi, `wokwi.toml` points to `firmware/build/firmware.elf` |
| Build | `make build` (Docker image `stm32-build`: Ubuntu 22.04, arm-none-eabi-gcc 10.3.1, `-Og`) |
| Simulator CLI | `wokwi-cli` v0.27.1 (Wokwi API 1.0.0-20261111-ga74d4d0e); the token is in the environment variable `WOKWI_CLI_TOKEN` and is never committed |
| Host | Python 3.10 or newer (standard library only), bash, `cc` for the host tests and the soak, Docker for the build and the memory analysis |
| Logic analyzer | the Wokwi `wokwi-logic-analyzer` part `logic1`, buffer 1000000 samples, trigger off, 8 channels, wiring in section 7.4; the VCD has a 1 ns timescale and a 25 ns sampling grid |

**Cost model of a simulator run** (measured on the runs in `docs/captures/lr3/*.cli.log`):

    real time  =  about 9 s start  +  simulated seconds  +  about 0.7 s per scenario step

- A Wokwi CLI session ends after 5 minutes of real time (`--timeout` is in simulated
  milliseconds and does not lift this). Scenarios are written to fit: S1 is 32 steps for 256 s
  (about 5 min, the longest one), S2 and S3 about 3.9 min.
- The account has a monthly allowance of simulator minutes as well (50 min per 30 days when this
  was first looked up; check the current plan). Group 1 of the re-run plan costs about 3.5 min,
  group 2 about 11 min.
- The firmware clock runs 74..96 ppm fast against the simulation time (S1: 250024 ms of firmware
  time for 250000 ms of scenario delays). It does not matter below 1 ms and adds up to 0.01 % over
  minutes.
- A run with a VCD file produces large files for long scenarios, so only short scenarios are
  captured (`tools/lr3_capture.sh`); the long ones are measured by the firmware (`tools/lr3_run.sh`).

**One-time setup:**

    make build                       # firmware/build/firmware.elf
    export WOKWI_CLI_TOKEN=...       # your token
    tests/run.sh                     # host tests: must print ALL TESTS PASSED
    tools/lr3_regen.sh               # rebuilds every report from the committed logs, no simulator

`tools/lr3_regen.sh` is the quick check that the host scripts of the working tree give the numbers
in this file (about 2 s). `git diff --stat docs/metrics` after it must be empty.

## 4. Scenarios and how to reproduce them

Run everything from the repository root. The firmware has to be built first (`make build`);
rebuild it whenever the firmware changed and note the build as described in section 2.

### 4.1 Overview

| Id | LR3 item | Scenario (`scenarios/`) | Command | Simulated / real time | Output |
|---|---|---|---|---|---|
| S1 idle | 6.1 | `s1-idle.yaml` (32 steps) | `tools/lr3_run.sh s1-idle 600000` | 256 s / about 5 min | `stats-s1-idle.txt` |
| S2 nominal | 6.1, 6.2.6 | `s2-nominal.yaml` (128 steps) | `tools/lr3_run.sh s2-nominal 600000` | 129 s / about 3.9 min | `stats-s2-nominal.txt` |
| S3 worst | 6.1, 6.2.1, 6.2.2, 6.2.3 | `s3-worst.yaml` (246 steps) | `tools/lr3_run.sh s3-worst 600000` | 47 s / about 3.9 min | `stats-s3-worst.txt` |
| E1 idle | 6.2.2 | `e1-idle.yaml` (168 steps) | `tools/lr3_run.sh e1-idle 600000` | 18.5 s / about 2.4 min | `stats-e1-idle.txt` |
| E1 on the analyzer | 6.2.2 | `stow.yaml`, `stow-busy.yaml` | `tools/lr3_capture.sh stow event`, `tools/lr3_capture.sh stow-busy event` | 6.5 s and 6.2 s / 0.5 min each | `analyzer-stow.txt`, `analyzer-stow-busy.txt` |
| `stats` reply on the analyzer | two methods | `stats.yaml` | `tools/lr3_capture.sh stats loop` | 3.2 s / 0.4 min | `analyzer-stats.txt` |
| Reply latency of a burst | 6.2.3 | `burst.yaml` with `burst.diagram.json` | `tools/lr3_capture.sh burst burst` | 1.4 s / 0.3 min | `analyzer-burst.txt`, `startup.txt` |
| Flood and ring | 6.2.3, 6.2.5 | `flood.yaml` (64 steps) | `tools/lr3_capture.sh flood flood 60000` | 6.3 s / 1 min | `analyzer-flood.txt` |
| Breaking point, UART | 6.2.5 | `sweep-uart.yaml` (130 steps) | `tools/lr3_capture.sh sweep-uart sweep 60000` | 4.7 s / 1.7 min | `analyzer-sweep-uart.txt` |
| Breaking point, milliseconds per pass | 6.2.5 | `sweep-ms.yaml` (114 steps) | `tools/lr3_capture.sh sweep-ms sweep 60000` | 9.3 s / 1.6 min | `analyzer-sweep-ms.txt` |
| Smoke test of `load` | 6.2.5 | `load.yaml` | `tools/lr3_capture.sh load loop` | 0.5 s / 0.3 min | `analyzer-load.txt` |
| Device failure | 6.2.4 | `nosd-log.yaml` with `nosd-log.diagram.json` | `tools/lr3_run.sh nosd-log 600000` | 18 s / 0.7 min | `stats-nosd-log.txt` |
| Park (T3) | FR-06 | `park.yaml` (234 steps) | `tools/lr3_run.sh park 600000` | 39.5 s / 3.5 min | `stats-park.txt` |
| Start-up | 6.2.7 | the capture of `burst` | `python3 tools/lr3_startup.py docs/captures/lr3/burst.vcd` | (no run) | `startup.txt` |
| Soak of 2 h | 6.2.6 | host program, not the simulator | `tests/soak.sh 2` | under 1 s | `soak.txt` |
| Memory | 5.4 | the build | `scripts/lr3_memory.sh` | under 1 min | `memory.txt` |

The simulated time of a capture is the length of its VCD; the real time is read from the
`.cli.log` for S2, S3, e1-idle, nosd-log and park and taken from the cost model of section 3 for
the others (an estimate).

Outputs: the serial log is `docs/captures/lr3/<name>.serial.log`, the `wokwi-cli` log is
`<name>.cli.log` (written by `lr3_run.sh` only), the analyzer capture is `<name>.vcd`, the report
is in `docs/metrics/`. Re-running a scenario overwrites its files, so commit the old ones first if
they are still needed.

`lr3_run.sh` takes the diagram `scenarios/<name>.diagram.json` when there is one and
`scenarios/lr3.diagram.json` otherwise. `lr3_capture.sh` does the same and adds `--vcd-file`.
Its last argument is the simulated timeout in ms (default 20000); a scenario that does not finish
in that time is reported as failed.

The long scenarios are generated and are committed as files, so the run does not need the
generator. `python3 tools/lr3_gen.py <s1|s2|s3|e1|burst|nosd|park>` writes `scenarios/<name>.yaml`
again, byte for byte (seeded random numbers, comment lines `# window` and `# press` are read back
by `lr3_stats.py`; do not edit them). The hand-written scenarios are `flood`, `load`, `stats`,
`stow`, `stow-busy`, `sweep-ms`, `sweep-uart`.

### 4.2 What each scenario does and what to read from it

**S1 idle.** Potentiometers constant, no commands, no presses; one window of 250 s, 5000 T2 runs.
The floor of every metric. Read: missed 0, T2 late max, loop pass max, CPU load, stack.

**S2 nominal.** The scenario is built from ticks of 4 s (32 ticks). At every tick pot 2 steps
through 0.9, 0.5, 0.1, 0.5 so that the tracker moves, and four console commands go out in one
write (taken in turn from `track`, `adc`, `help`, `button`, `log`: one command per second on
average). Logging is on. At every eighth tick (ticks 3, 11, 19, 27) there is one press followed by
`resume`: 4 presses. One window of 129 s. Read: T2 late, `cmd` max against 100 ms, log flush max,
E1 on presses that follow a command group, rx dropped.

**S3 worst.** The same ticks, 9 of them. Each tick sends the group of four commands, then, by the
tick number modulo 3: an `sd write 16000` alone (ticks 0, 3, 6), one press at a random moment
(1, 4, 7, seeded) or a burst of 20 `track` lines in one write (2, 5, 8). After that every tick
sends three presses during an `sd write`: the line `sd write 16000`, then the press 3, 5, ..., 27
ms later (13 offsets used in turn, two or three presses each, 27 presses in all), then `resume`.
Every press is held 10 ms and followed by 250 ms (more than the 200 ms lockout). The epilogue sends
`stats`, `log flush`, `log`, three `log get` and `button`. So there are 30 presses: 27 in the SD
phase and 3 at random moments. One window of 47.3 s (the YAML header says 36 s, which is the
nominal 9 ticks of 4 s; the presses add the rest). Read: E1 max and the table "press during an sd
write" (latency against offset), T2 late max, `cmd` max, ring max.

**E1 idle** (6.2.2 in a quiet system). `stats reset`, 40 presses, each preceded by a random lead
of 1..99 ms (seed 2026), no commands in flight, log off. A second window calls the diagnostic
`time` command three times (0.34 s each) to show a long operation against T2. Read: E1 min, average,
median, p90, max, the 1 ms histogram, accepted = 40; in window 2 the skipped T2 runs and
late max (a deliberate MISSED).

**stow and stow-busy** (analyzer). One press in an idle loop and one press while an SD block write
blocks the loop. Read: edge to EVENT pulse, edge to the first servo pulse with a new width, the
difference between the analyzer and the `lat_us` of the `EVT stow` line (the second method).

**stats** (analyzer). One `stats` command with the analyzer on D0..D7. Read: the loop-pass table
(P, margin to the byte time), the pulse width of T2, and the comparison table with the firmware
counters (the two-method comparison).

**burst.** One `track` line alone, then 20 lines in one write, with the receive line PA3 on D3
and the transmit line PA2 on D4 (`burst.diagram.json`). Read: reply start and end per line
against 100 ms, the time per queued reply, the start-up milestones (`startup.txt`).

**flood.** Eight `load` windows (see 7.1): six of 30 lines of 48 bytes in one write at an extra
wait per pass of 0, 8, 17, 34, 68 and 100 us; `stats-reply` (the line `stats` and 10 lines after
it); `sd-write` (the line `sd write 16000` and 5 lines). Read: the table RECEIVE BUFFER
(arrived, read, dropped, ring max), `ok` and `bad` replies, T2 late and skipped runs per window.

**sweep-uart.** Eleven `load` windows with an extra wait of 0 to 100 us per pass and four 48-byte
lines (`resume` and 40 digits) in each; one of them is a control window whose lines overlap a T2
run (the bytes lost there are lost to T2, not to the length of the pass). Read: bytes received
(192 expected per window), lost, ring max, `isr_avg`, the longest pass P.

**sweep-ms.** Seven `load` windows with 1 to 30 ms per pass and three presses in each. Read: E1
firmware and pin reaction, T2 late max and missed runs, and the lines "first at X" under BREAKING
POINTS.

**nosd-log.** The MISO wire of the card is cut (`nosd-log.diagram.json`, the self-test reports
`passed=4 failed=1`), tracker moving, `log on`, 12 s window, then `sd write` three times. Read:
the `log` counters (`seq`, `dropped`, `errors`), the three `CMD sd-write FAIL step=init status=3`
lines, T2 late max and missed runs.

**park.** Sixteen cycles. In each, the light on one channel goes out (pot 1 to 0) and `track` is
sent 920 ms and 1030 ms later; then the light returns for 150..200 ms, goes out again for 900 ms
(a cloud shorter than 1 s) and returns, and `track` is sent once more. Read: parked 0 at 920 ms
and 1 at 1030 ms in 16 of 16 cycles, the panel at the safe position, parked 0 after the cloud, the
margin to 1.1 s.

### 4.3 Not run in the simulator (6.2.6 to 6.2.8 in part)

| Item | Why | What replaces it |
|---|---|---|
| Run of 1 h or more on the target, the 358 s wrap of the cycle counter | a session is cut at 5 min | `tests/soak.sh 2`: the real `app.c`, the tracker, the logger and the console on fake drivers and fake time, 2 h of virtual time, S2 load, 20 wraps of the counter; result in section 5.4 |
| Restart of the firmware | no reset command and no watchdog | none; the restart behaviour of the log is described in `docs/limitations.md` |
| Power loss during a write | the firmware has no internal flash writes and a scenario cannot cut the power | none; log records carry a checksum (sum of the first 28 bytes with a seed, not a CRC) and 3 of 3 read back right in S2 and S3, 65536 of 65536 in the host soak |
| Card inserted again at run time | a scenario cannot connect a wire | the host test of the retry after a failed flush |
| A device that answers but never finishes | the simulated card cannot stall | the bounds computed in limitation 6 |
| T3 under the S3 load | `park` ran on its own | argued in `docs/limitations.md`: T2 never skips in S3, so T3 is at most about 1.02 s against 1.1 s |

## 5. Final metrics

The build letter is the one of section 2. A dash is "not measured in this scenario". "ok" and
"MISSED" are the verdicts of `tools/lr3_stats.py` against the deadlines of `docs/tasks.md`.

### 5.1 Performance in S1, S2, S3 (build C)

Source: `docs/metrics/stats-s1-idle.txt`, `stats-s2-nominal.txt`, `stats-s3-worst.txt`.

| Metric | Deadline | S1 idle | S2 nominal | S3 worst | S3 minus S1 |
|---|---|---|---|---|---|
| window | - | 250.0 s | 129.1 s | 47.3 s | - |
| T2 runs / skipped | skipped 0 | 5000 / 0 | 2582 / 0 | 946 / 0 | - |
| T2 start late, max | 25000 us | 5861 us | 10536 us | 20956 us | +15095 us |
| T2 start late, avg | - | 3858 us | 3293 us | 3481 us | -377 us |
| T2 period, max | - | 54005 us | 59678 us | 66569 us | +12564 us |
| T1 sense, max | 5000 us | 602 us | 738 us | 738 us | +136 us |
| T2 body, max | - | 833 us | 1151 us | 1151 us | +318 us |
| loop pass, avg | - | 5999 us | 5970 us | 5566 us | -433 us |
| loop pass, max | - | 6007 us | 20005 us | 24624 us | +18617 us |
| T4 commands, n | - | 1 | 133 | 136 | - |
| T4 command, avg | - | 2332 us | 8275 us | 11695 us | - |
| T4 command, max | 100000 us | 2332 us | 11834 us | 19928 us | +17596 us |
| T5 block writes, n | - | 0 | 80 | 28 | - |
| T5 block write, max | - | - | 13496 us | 13847 us | - |
| E1 stow, n | at least 30 | 0 | 5 (4 sent) | 30 | - |
| E1 stow, max | 10000 us | - | 5610 us (ok) | **18511 us (MISSED)** | - |
| CPU load | - | 16 permille | 34 permille | 59 permille | +43 |
| rx bytes / dropped | dropped 0 | 6 / 0 | 699 / 0 | 1067 / 0 | - |
| rx ring max (of 256) | - | 1 | 55 | 114 | +113 |
| stack used / heap | - | 916 B / 0 | 924 B / 0 | 940 B / 0 | +24 B |
| log read back | - | - | 3 of 3 checksums ok | 3 of 3 checksums ok | - |

Verdicts: T1, T2 start, T4 execution and the receive buffer are met in all three scenarios; E1 is
**not met in S3**: 10 of the 27 presses that came during an `sd write` took more than 10 ms
(a press `d` ms after the line waits about 21 ms minus `d`), the 3 random presses took at most
5.0 ms. S2 has 5 accepted presses for 4 sent: the stow lockout kept an interrupt time stamp one
tick behind (fixed in D, commit `19b0119`). S3 ran before the fix and shows 30 of 30.

Calculation behind the S3 numbers (`docs/limitations.md`, worst-case model): reaction(max) = B + H,
with B the longest stretch in which the loop does not return to `button_poll` and H the handler
(0.10..0.12 ms). Press 3 ms after an `sd write` line: B about 21 ms, so 18 ms expected, 18.5 ms
measured.

### 5.2 E1, the reaction to the pushbutton

| Run | Build | n | min | avg | median | p90 | max | Deadline 10 ms |
|---|---|---|---|---|---|---|---|---|
| e1-idle, random leads | D | 40 (40 accepted) | 0.338 ms | 2.942 ms | 2.685 ms | 5.510 ms | 6.113 ms | ok, margin 3.9 ms |
| S3, all presses | C | 30 | 0.576 ms | 7.766 ms | 5.439 ms | 16.403 ms | 18.511 ms | MISSED, 10 over |
| S3, presses during `sd write` | C | 27 | 0.576 ms | 8.171 ms | 5.831 ms | 16.403 ms | 18.511 ms | 10 over |
| S3, random moments | C | 3 | 3.664 ms | 4.122 ms | 3.685 ms | 5.017 ms | 5.017 ms | ok |
| S2, after a command group | C | 5 | 0.270 ms | 1.356 ms | 0.292 ms | 5.610 ms | 5.610 ms | ok (phase-locked, not an idle distribution) |

On the analyzer (build A, `docs/metrics/analyzer-stow-busy.txt`), one press while an SD block
write holds the loop: the edge came 712 us after the loop became busy, the busy stretch lasted
24.8 ms, the EVENT pulse started 18.93 ms after the edge and lasted 101.9 us, the firmware printed
`lat_us=19020` (analyzer minus firmware: +11.0 us), the new servo pulse started 31.37 ms after the
edge against 30 ms allowed. In the idle `stow` capture the EVENT pulse is 102.2 us and 119.0 us
wide.

### 5.3 Two methods for one metric (build A)

`docs/metrics/analyzer-stats.txt`, one `stats` command, 60 T2 runs:

| Metric | Firmware counter | Logic analyzer | Difference | Why |
|---|---|---|---|---|
| T2 duration, max | 850 us | 844.7 us | -5.3 us | the PB4 pulse starts after the time stamp that starts `track` |
| T2 start to start, max | 54005 us | 54000.5 us | -4.5 us | same, at the edges of the window |
| T2 start jitter | 4007 us | 4228 us | +221 us | firmware clock 74..96 ppm fast against the simulation, 3 s at 74 ppm = 221 us |
| loop pass period, max | 6005 us | 6004.2 us | -0.8 us | |
| passes counted | 505 | 505 | 0 | |
| `stow` latency | 19020 us | 19031 us (to the end of the pulse) | +11.0 us | the analyzer starts at the pin edge, the firmware at the time stamp in the interrupt |

The `stow` row is from `analyzer-stow-busy.txt`, the others from `analyzer-stats.txt`. The same
`stats` capture gives the longest pass of the run: 82.743 ms (the `stats` reply, no T2 run in it).

### 5.4 The scenarios of LR3 section 6.2

| Item | Result | Build | Source |
|---|---|---|---|
| 1 long operation against the periodic task | S3: T2 late max 20.956 ms against 5.861 ms in S1, 0 skipped. `time` (342 ms): 18 T2 runs skipped in three calls, T2 late max 318.1 ms, CPU 998 permille. Reply to `stats` in a window: 1 run skipped, late 55.291 ms (flood, `stats-reply`) | C, D | `stats-s3-worst.txt`, `stats-e1-idle.txt`, `analyzer-flood.txt` |
| 2 event during a long operation | 30 presses in S3: max 18.511 ms, 10 of 27 over 10 ms; analyzer: 31.37 ms to the new servo pulse | C, A | `stats-s3-worst.txt`, `analyzer-stow-busy.txt` |
| 3 command flood | 1440 bytes in 125 ms at the line rate: lost 53, 134, 238, 415, 575, 657 at an extra wait of 0, 8, 17, 34, 68, 100 us per pass; `stats` reply: 224 of 486 lost; `sd write`: 0 of 255 lost, ring max 240 of 256. Reply time: one line 12.094 ms, in a burst of 20 each reply adds 11.736 ms, the 9th reply ends at 101.9 ms (the first late one), 12 of 20 are late, the last ends at 225.87 ms | C, D | `analyzer-flood.txt`, `analyzer-burst.txt` |
| 4 device failure | card absent: the log keeps 16 records, drops 144, 12 flush errors, `sd write` fails in 7.868 ms (init about 4 ms); T2: 240 runs, 0 skipped, late max 5.894 ms. Stuck-device blocking times are computed, not measured (0.1 s to 4.9 s) | D | `stats-nosd-log.txt`, `docs/limitations.md` limitation 6 |
| 5 breaking point | UART without the ring (build B): first byte lost at an extra wait of 17 us per pass, 100 of 192 lost at 100 us. With the ring (C): 192 of 192 in all 11 windows, `isr_avg` 143 cycles. Per-pass milliseconds (B): E1 breaks at +6 ms (max 11.5 ms), T2 at +20 ms (late max 27.2 ms). Flood (C): the ring breaks at 1440 bytes in 125 ms even with no extra wait | B, C | `analyzer-sweep-uart-poll.txt`, `analyzer-sweep-uart.txt`, `analyzer-sweep-ms.txt`, `analyzer-flood.txt` |
| 6 long run | not possible in the simulator. Host soak, 2 h of virtual time, S2 load: 144040 T2 runs, 0 missed, 20 wraps of the counter, no backward time stamp, log full after 6533 s (108.9 min), then 6748 records dropped, 225 presses all accepted, 65536 records read back, 0 bad checksums; heap 0 in every scenario | D (host) | `soak.txt` |
| 7 start-up | reset to the first byte of the banner 4.8 ms; self-test finished 48.9 ms; main loop running 54.7 ms (T6: 1000 ms, met); first T2 run starts 102.7 ms and ends 103.5 ms. Restart: not run | D | `startup.txt` |
| 8 power loss during a write | not applicable (no internal flash writes); see 4.3 | - | - |

Notes for item 3: a line of 100 bytes takes 9.415 ms to send, 93.9 us per byte against 86.7 us
theoretical (the polled transmitter leaves a gap between bytes), which bounds the burst at about 10
lines in 100 ms. The interrupt handler takes 143..144 cycles per byte in `sweep-uart` (11.9 us,
13.8 % of the CPU at the line rate); in the flood windows `isr_avg` is 129..151 cycles.

### 5.5 T3 (park)

`docs/metrics/stats-park.txt`, build D, 16 cycles: at 920 ms after the light went out the panel
was not parked and had 18 or 19 dark samples (16 of 16 right); at 1030 ms it was parked at the safe
position with 20 dark samples (16 of 16); with the light back, 0 dark samples and not parked (16 of
16). Missed T2 runs 0. Deadline 1.1 s: met, margin at least 70 ms.

### 5.6 Memory (build D)

`docs/metrics/memory.txt` (`scripts/lr3_memory.sh`, `-Og -fstack-usage`, arm-none-eabi-gcc 10.3.1).

| Item | Value |
|---|---|
| flash | 31708 of 32768 B, 96.77 %, 1060 B free |
| RAM (linker figure: static plus reserve) | 3832 of 12288 B, 31.18 % |
| static data (`.data` 112 B plus `.bss` 2184 B) | 2296 B |
| reserve in the linker script | heap 512 B, stack 1024 B |
| heap used (`_sbrk`) | 0 B in every scenario |
| stack, static worst case (`-fstack-usage`) | 1016 B against the 1024 B budget |
| stack, measured by painting (largest in S1, S2, S3) | 940 B |
| flash by group | our code 12355 B, HAL and startup 10589 B, libc_nano 4212 B, string constants 3333 B, libgcc 936 B |
| largest objects | `app` 1416 B RAM (62 % of static); `app.o` 6031 B flash; `rx_ring` 268 B |
| interrupt priorities | EXTI button 0, USART2 1, SysTick 3 |

Stack painting does not see the stack used before `main`, and the result is a high-water mark
since reset (not cleared by `stats reset`).

### 5.7 Summary against the PRD tasks

| Task | Deadline | Worst in S3 | Verdict |
|---|---|---|---|
| T1 sense | 5 ms | 0.738 ms | met |
| T2 track, start late | 25 ms | 20.956 ms | met, margin 4.0 ms |
| T3 park | 1.1 s | at most 1.03 s measured on its own | met |
| T4 console, execution | 100 ms | 19.928 ms | met |
| T4 console, reply in a burst of 20 | 100 ms | 225.9 ms for the last | **not met** for lines 9 to 20 |
| T4 console, no byte lost | 0 | 0 lost in S3; 53..657 lost in a flood | met in S1..S3, not in a flood |
| T5 log | a block within 10 s | 28 blocks, 0 dropped | met |
| T6 start | 1 s | 54.7 ms | met |
| E1 reaction | 10 ms | 18.511 ms | **not met** |

## 6. Hypotheses for LR4

Copied from the summary table of `docs/limitations.md` (the reasoning is in the entry of the same
number). LR4 repeats the scenario in the third column on FreeRTOS and fills the last column.

| Id | Limitation | Scenario and metric | Baseline (build) | Hypothesis | LR4 result |
|---|---|---|---|---|---|
| H1a | 1 reaction | S3, E1 max over the same 30 presses, independent of the offset `d` | 18.511 ms (C) | at most 1 ms | |
| H1b | 1 reaction | e1-idle, E1 max | 6.113 ms (D) | at most 1 ms | |
| H1c | 1 reaction | stow-busy, edge to the new servo pulse | 31.37 ms (A) | at most about 21 ms (reaction plus one 20 ms frame) | |
| H2a | 2 jitter | S1, S2, S3, T2 start late max | 5.861 / 10.536 / 20.956 ms (C) | at most 2 ms in all three | |
| H2b | 2 jitter | window with a `stats` reply (flood, `stats-reply`): skipped T2 runs and late max | 1 run, 55.291 ms (C) | 0 runs, at most 2 ms | |
| H3a | 3 long commands | burst of 20 `track` lines: lines answered within 100 ms | 8 (D) | about 10, the limit of the wire | |
| H3b | 3 long commands | `time` window of e1-idle: skipped T2 runs | 18 (D) | 0 | |
| H4a | 4 input lost | flood, six windows: bytes lost of 1440 | 53 / 134 / 238 / 415 / 575 / 657 (C) | 0 in all six | |
| H4b | 4 input lost | flood, `sd-write` window | 0 of 255, ring max 240 (C) | 0 lost | |
| H4c | 4 input lost | flood, `stats-reply` window | 224 of 486 (C) | at most about 30 | |
| H5 | 5 shared data | at least 100 presses over e1-idle and S3: accepted equals sent, no backward time stamp | e1-idle 40 of 40 (D); S3 30 of 30 (C) | 0 anomalies; the time from the EXTI handler to the first instruction of the E1 task is measured on PB7 (no LR3 baseline) | |
| H6 | 6 failing device | nosd-log: T2 late max and E1 | T2 late max 5.894 ms, loop blocked about 4 ms once per second (D) | unaffected, at most 2 ms; `dropped` stays 144 | |
| H7a | 7 new task | sweep-ms with the load as the lowest-priority task: first X where E1 or T2 breaks | E1 at 6 ms, T2 at 20 ms (B) | no break up to 30 ms | |
| H7b | 7 new task | places to edit for one new periodic task | at least 4 | 2 (the task function and its creation) | |
| H8a | 8 memory | flash used before trimming | 96.77 % (D) | over 100 % | |
| H8b | 8 memory | RAM, same tool | 31.18 % (D) | about 40..55 % (an estimate); `heap_used` reported separately | |

Kernel figures that are not measured yet (external estimates from the FreeRTOS FAQ, to be measured
in LR4): kernel 5..10 KB of flash, 236 B of RAM, 64 B plus the stack per task, 76 B plus the
storage per queue.

### What the super loop does better (to be re-checked in LR4)

| Property | Value in LR3 | What to compare in LR4 |
|---|---|---|
| Determinism of the schedule | S1: 5000 runs, 0 skipped, T2 late max 5.861 ms, period max 54.005 ms (C); host soak: 144040 runs, 0 skipped | T2 start not later than one tick in S1 |
| Memory | heap 0 B, one stack, RAM 31.18 % | totals from the same tool, the kernel heap pool reported |
| Start-up | first loop pass 54.7 ms, first T2 run 102.7 ms (D) | the same milestones with the scheduler started |
| Explainability | every large latency is a sum of known sections (11.74 ms per queued reply = 8.667 + 0.75 + 2.33; E1 about 21 ms minus `d`) | whether the latencies can still be explained once tasks preempt each other |
| No bugs between tasks | no mutex, queue, inversion or deadlock; three interrupts with fixed priorities | the kernel objects used and every inversion or deadlock met |
| Low overhead | no tick work beyond SysTick; S1 CPU load 16 permille | S1 CPU load with the scheduler |

## 7. Measurement instrumentation

The full description with the cost of the instrumentation and the interpretation notes is
`docs/instrumentation.md`. This section lists what LR4 must reproduce.

### 7.1 Console commands

| Command | Reply / effect |
|---|---|
| `stats` | twelve `STAT` lines, then `CMD stats OK` |
| `stats reset` | `CMD stats-reset OK`; clears the statistics, the counters and the window start; the state of the product is not touched |
| `load <us> <ms>` | test aid: starts a new window like `stats reset`, makes every pass of the loop spend `<us>` microseconds in a busy wait, ends by itself after `<ms>` ms with the `STAT` lines and `EVT load done us=<us> ms=<window>`; also restarts the 50 ms grid of T2; `<us>` up to 50000, `<ms>` 1..60000 |
| `load` | `CMD load OK us=<last> active=<0 or 1>` |
| `time` | diagnostic: 40000 reads of the cycle counter, about 342 ms of blocking; prints the read cost and the number of backward steps; used as "a long operation" in e1-idle |
| `track`, `button`, `log` | state of the product: `track` the tracker and its counters, `button` the edges (`edges`, `accepted`, `ignored`), `log` the logger counters (`seq`, `stored`, `blocks`, `dropped`, `errors`, `flushes`, `flush_max_us`) |
| `log on`, `log off`, `log flush`, `log get <n>` | switch the logger, write the open block, read record `n` back with its checksum |
| `sd write <block>`, `sd read <block>`, `sd init` | the long operations of S3 (13.4..16 ms for a block write) |
| `stow`, `resume`, `zero`, `deadband`, `adc`, `servo`, `selftest`, `help` | product commands used by the scenarios |

### 7.2 Log formats

All lines end with `\r\n`; the host scripts also accept lines glued together (the analyzer and the
stats parser split them).

    STAT name=<n> n=<count> min_us=<min> avg_us=<avg> max_us=<max>      names: loop sense track late period cmd logflush stow
    STAT load window_ms=<ms> busy_ms=<ms> load_pm=<permille>
    STAT count runs=<n> missed=<n> rx_bytes=<n> rx_errors=<n> passes=<n>
    STAT rx init=<status> dropped=<n> ring_max=<n> errors=<n> isr_n=<n> isr_avg=<cycles> isr_max=<cycles>
    STAT mem ram=<B> static=<B> heap_used=<B> heap_res=<B> stack_used=<B> stack_res=<B> stack_free=<B>
    EVT stow t_us=<stamp of the edge> lat_us=<edge to the end of the handler>
    EVT load done us=<us> ms=<window>
    CMD <name> OK ...    CMD <name> FAIL ...

Time stamps come from the cycle counter in the firmware and never from the arrival time on the
host (LR3 section 5.2). `EVT stow t_us` is a 32-bit count of microseconds from reset.

### 7.3 Definitions of the metrics

| Name | Definition |
|---|---|
| `loop` | time between two consecutive passes of the main loop while tracking runs (average = window time divided by passes) |
| `sense` (T1) | the four ADC conversions, cycle counter before and after, successful reads only |
| `track` (T2 body) | from the moment the run is found due until it returns, schedule bookkeeping and servo update included |
| `late` | how long after its nominal release (the absolute 50 ms grid `track_next`) T2 started |
| `period` | real time between two starts of T2 |
| `missed` | releases skipped: a run that finds itself more than one period late skips the releases it missed |
| `cmd` (T4) | from the end of the command line until its reply has been sent, the transmission included |
| `logflush` (T5) | one SD block write of the log |
| `stow` (E1) | from the falling edge on the button pin (time stamp taken in the interrupt) to the servo update |
| `load_pm` | share of the window in which task code ran (due T2 runs, log flushes, commands, the `load` wait), in permille, at most 1000; waiting for a byte counts as idle; the stow handler is not counted |
| `ring_max` | most bytes in the 256-byte receive ring at one time |
| `isr_*` | time spent in the USART2 handler in cycles of the 12 MHz clock (SysTick `VAL` at entry and exit; the exception entry and return are outside) |
| `stack_used` | deepest stack found by painting the RAM with `0xA5A5A5A5` |
| the cycle counter | the Cortex-M0+ has no DWT: it is built from the tick count, SysTick `VAL` and the pending flag with a retry loop; 12 cycles per microsecond; wraps every 357.9 s; a read costs 88 cycles (7.3 us); differences of unsigned values are right for spans below the wrap |

### 7.4 Service pins and the logic analyzer

Wiring for all LR3 captures: `scenarios/lr3.diagram.json`. Analyzer: `wokwi-logic-analyzer` named
`logic1`, `bufferSize` 1000000, `triggerMode` off, ground connected, channels D0..D7:

| Channel | Pin | Signal | Driven by |
|---|---|---|---|
| D0 | PB6 | loop busy: low while the loop waits for a byte, high for the rest of the pass | UART adapter, two register writes per pass |
| D1 | PB4 | T2: high from the start of `track_due` to the time stamp that ends the `track` statistic | `app.c` |
| D2 | PB7 | EVENT: high while the stow handler runs | `app.c` |
| D3 | PB5 | the button (in `burst.diagram.json`: PA3, the receive line) | button |
| D4 | PA2 | UART TX | USART2 |
| D5 | PC6 | servo 1 PWM | TIM3 |
| D6 | PB9 | SD chip select | `plat_spi` |
| D7 | PB3 | ADC marker, high during one conversion | `plat_adc` |

`nosd-log.diagram.json` is `lr3.diagram.json` without the wire `sd1:DO`. PB4, PB6 and PB7 are
outputs for the analyzer only. If the pass margin is ever lost, `PROBE_BUSY_ENABLED` in
`firmware/platform/src/probe_pins.h` switches the PB6 writes off. The LR2 files
(`diagram.json`, `scenarios/nosd.diagram.json`, `scenarios/nosd.yaml`) belong to LR2, are
untouched and are not used for LR3 results.

Reading rules: one pass is rising edge to rising edge of D0, `P` is the high width of D0
(passes without a T2 run, below 300 us); T2 is the width of the D1 pulse and the start to start
time; E1 is from the first falling edge on D3 to the start and end of the D2 pulse and to the first
servo pulse with a new width. The VCD has a 1 ns timescale and a 25 ns grid.

### 7.5 Host scripts

All are in `tools/`, standard library only.

| Script | Input | Output |
|---|---|---|
| `lr3_run.sh <scenario> [timeout_ms]` | scenario file | serial log, `wokwi-cli` log with real-time stamps, `docs/metrics/stats-<name>.txt` (default timeout 600000) |
| `lr3_capture.sh <scenario> <loop\|event\|sweep\|flood\|burst> [timeout_ms]` | scenario file | `.vcd`, serial log, `docs/metrics/analyzer-<name>.txt` (default timeout 20000) |
| `lr3_stats.py <serial.log> --scenario <yaml>` | serial log of S1, S2, S3, e1-idle, nosd-log | per-window table with deadlines and verdicts, E1 per kind of press, button and log lines |
| `lr3_analyze.py <vcd> <loop\|event> --serial <log>` | capture | loop pass, T2, T1, CPU time, E1 timings, comparison with the firmware numbers |
| `lr3_sweep.py <vcd> --serial <log> [--names n=name,...]` | capture of a `load` sweep or of the flood | the tables UART AND LOAD, RECEIVE BUFFER, DEADLINES and BREAKING POINTS |
| `lr3_burst.py <vcd> --serial <log>` | capture with `burst.diagram.json` | reply start and end per line, time per reply |
| `lr3_park.py <serial.log> --scenario <yaml>` | serial log of `park` | the 16-cycle table and the T3 verdict |
| `lr3_startup.py <vcd>` | any capture that includes the reset (`burst.vcd`) | start-up milestones, the UART lines before the first T2 run, the T6 verdict |
| `lr3_memory.py` (via `scripts/lr3_memory.sh`) | the build with `-fstack-usage` | `memory.txt` |
| `lr3_gen.py <s1\|s2\|s3\|e1\|burst\|nosd\|park>` | seed 2026 | the scenario file, byte for byte |
| `lr3_regen.sh` | the committed logs and captures | every report in `docs/metrics` except `memory.txt` and `soak.txt`, no simulator needed |
| `tests/soak.sh [hours]` | the real `app.c` on fake drivers | `soak.txt` content on stdout (the file is the committed copy of a 2 h run) |
| `tests/run.sh`, `make check` | the host tests | nine test programs (2390 checks at the time of writing) and the dependency check |

### 7.6 Known quirks of the instrumentation

- **Clock scale.** The firmware clock runs 74..96 ppm fast against the simulation time. Spans of
  seconds differ by up to 0.01 %; durations below 1 ms are not affected. This is the reason for the
  221 us in the jitter comparison.
- **Byte time.** The real line rate gives 86.7 us per byte, but the polled transmitter in the
  simulator needs 93.9 us per byte, so a reply of 100 bytes lasts 9.4 ms and not 8.7 ms.
- **The simulator does not set the USART overrun flag**, so `rx_errors` stayed 0 when bytes were
  lost before the ring existed; bytes lost to a full ring are counted in `dropped`.
- **After a ring overflow the framing of lines is lost.** A line end can be dropped, so two lines
  run together and the next command is damaged. The flood scenario sends an empty line after each
  window to clear it.
- **`stats` blocks the loop for 82.7 ms (build A) to about 90 ms (D, estimate)**, so a T2 release
  that falls into the reply is late by up to that time. Read the statistics at the end of a measurement; scenarios send `stats` last in a
  window and `stats reset` right after.
- **`load` restarts the T2 grid**, so the runs in a window are due 50, 100, ... ms after the
  command. The first run of the window replaces the pending release, it is not counted as missed.
- **The `late` statistic is quantised.** An idle pass is 6 ms (a 5 ms wait for a byte plus the tick
  alignment), so the T2 start lateness shows levels at about 1.9, 3.9 and 5.9 ms: 50 ms modulo 6 ms.
- **`time` is a diagnostic, not a product command.** Its T4 verdict "MISSED" in window 2 of e1-idle
  is expected.
- **Presses in S2** are sent right after a command group, so they are phase-locked with the loop; the
  0.3 ms median is not an idle distribution. The idle distribution is the e1-idle run.
- **The S3 header says 36 s**; the window is 47.3 s (see 4.2). The header was not changed, so the
  scenario file stays identical to the one that was run.
- **Analyzer pins** add about 1 us to the loop pass (PB6) and about 4 us per 50 ms to T2 (PB4).
- **Stack painting** misses the stack used before `main` and a word equal to the pattern.

## 8. Carrying the instrumentation to FreeRTOS

These are the points where the LR3 code depends on the super loop. Each has to be given a
counterpart in LR4 or its absence reported.

| LR3 element | Depends on | What LR4 has to provide |
|---|---|---|
| `loop` statistic, `passes`, loop-pass `P` on PB6 | one thread with a pass | there is no pass: report the idle-task gap or the run time of each task instead, and say so in the table; keep PB6 as "the CPU is not in the idle task" |
| `late` of T2 | the absolute grid `track_next` | `vTaskDelayUntil` on the same 50 ms grid; the nominal release is the grid time, the start is the time stamp at the first instruction of the task |
| `track`, `sense`, `cmd`, `logflush` | cycle counter reads around the code | the same reads, so the numbers are comparable; the counter is built from SysTick and the tick (FreeRTOS owns the tick: keep the time base the same or measure the difference) |
| `load` command and the busy wait per pass | a pass | a busy wait of X in a task of the lowest priority, started by the same command, ending by itself; keep the arguments and the `EVT load done` line |
| `load_pm` | counting task code time | per-task run time from the counter, summed over the tasks other than idle |
| E1 `lat_us` and PB7 | the handler runs in `button_poll` | the same two time stamps (edge in the interrupt, end of the handler in the E1 task) and the same pin |
| `stats` reply of 90 ms | polled TX | the reply may be sent differently, but `STAT` formats and names stay |
| stack by painting | one stack | one value per task; `stack_used` of the product is the sum, report both |
| `heap_used` | `_sbrk` | the kernel heap (`heap_1..heap_5`) reported separately from the C library |
| SysTick priority 3, USART2 1, EXTI 0 | the interrupt plan | state the new plan; the kernel needs the tick at the lowest priority |

Two things that must not be lost on the way: the scenarios send the same bytes at the same time
(the T2 grid is restarted by `load`, so the scenarios do not depend on when the board started), and
the `STAT` lines are parsed by the same scripts, so a new field is appended at the end of a line,
never inserted.

## 9. Checklist for repeating a scenario

1. `git status --short firmware` is empty, `make build` is done, the build is noted (section 2).
2. `tests/run.sh` prints ALL TESTS PASSED.
3. `WOKWI_CLI_TOKEN` is set; the scenario fits the 5 min cap (section 4.1).
4. Run the command of the table in 4.1; keep the `real time` line of `lr3_run.sh`.
5. Check the report: no `FAIL` lines other than those the scenario expects (nosd-log, `time`),
   `arrived` equals 1440, 486 and 255 in the flood, the serial log has the number of windows the
   YAML comment lines promise.
6. Compare with the tables of section 5 by the maximum; explain a difference over 10..20 %.
7. Commit the serial log, the capture and the report together, with the build in the message.
8. `tools/lr3_regen.sh` followed by `git diff --stat docs/metrics` shows nothing.

## 10. Known gaps

- S3 has 27 presses in the SD phase and 3 at random moments, 30 in all. LR3 asks for 30 to 50
  repeats of the event during a long operation, so the total meets it, but the SD-phase group is
  27 and not 30. Ten ticks instead of nine in `lr3_gen.py` (`TICKS["s3"]`, one value) would give
  30 plus 3 and about 9 s more real time; it is not done, because it moves the S3 baseline.
- Build letters of section 2: the A and B figures are of the old firmware until the re-run.
- The simulator results are simulator times. On a real board the SysTick period, the SPI timing of
  the card and the PWM frame can differ; the shape of the limits (a reaction equal to the longest
  blocking section) does not.
- The items of section 4.3 were not run, and are not claimed.

## Change log of the baseline

| Date | Change |
|---|---|
| 2026-11-13 | first version of this file; `tools/lr3_startup.py`, `tools/lr3_regen.sh` and `docs/metrics/startup.txt` added with it |
