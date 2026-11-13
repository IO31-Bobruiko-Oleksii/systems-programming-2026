# Instrumentation: the `stats` command

LR3 measures the firmware with code that runs inside the firmware. This file says what is
measured, how, and what the measuring itself costs. All times are simulator times (Wokwi, core
clock 12 MHz), see the report for what that means.

The scenarios, the final numbers and the steps to repeat every run are in
`docs/baseline-lab4.md`. The firmware changed while the measurements were made, so a result
below carries the letter of the build it was taken on (A to D) where the build matters;
`docs/baseline-lab4.md` (section 2) lists the commits and the captures of each build. Where a
number was written down before a run as a prediction, it is kept next to the measured value.

## Commands

| Command | Reply |
|---|---|
| `stats` | twelve lines starting with `STAT`, then `CMD stats OK`: 13 lines, about 760 characters (S1 log, build C) |
| `stats reset` | `CMD stats-reset OK`, starts a new measurement window |
| anything else after `stats` | `CMD stats FAIL reason=usage` |
| `load <us> <ms>` | `CMD load OK us=<us> ms=<ms>`; at the end of the window the `STAT` lines and `EVT load done us=<us> ms=<window>` |
| `load` | `CMD load OK us=<last value> active=<0 or 1>` |
| `time` | `CMD time OK hz=<clock> read_cycles=<n> backsteps=<n>`; a diagnostic, not a product command |

`stats reset` clears the statistics, the counters and the window start. It does not touch the
state of the product (`track`, `button` and `log` keep their own counters).

`time` reads the cycle counter 40000 times in a row (20000 pairs). `read_cycles` is the smallest
difference between the two reads of a pair, `backsteps` the number of pairs in which the second
read was smaller than the first. Measured (build D, the three calls in window 2 of
`docs/metrics/stats-e1-idle.txt`): `read_cycles` 88 (7.3 us at 12 MHz), `backsteps` 0. The command
blocks the loop for about 342 ms (the `cmd` maximum of that window is 341805 us, which is 8.5 us
per read on average), so it serves as the "long operation" of the e1-idle scenario: three calls
skip 18 T2 releases and make T2 start up to 318 ms late.

## Lines of the reply

```
STAT name=<n> n=<count> min_us=<min> avg_us=<avg> max_us=<max>
STAT load window_ms=<ms> busy_ms=<ms> load_pm=<permille>
STAT count runs=<n> missed=<n> rx_bytes=<n> rx_errors=<n> passes=<n>
STAT rx init=<status> dropped=<n> ring_max=<n> errors=<n> isr_n=<n> isr_avg=<cycles> isr_max=<cycles>
STAT mem ram=<B> static=<B> heap_used=<B> heap_res=<B> stack_used=<B> stack_res=<B> stack_free=<B>
```

| Name | What is measured | How |
|---|---|---|
| `loop` | time between two consecutive passes of the main loop | cycle counter read at the start of every pass while tracking runs; the average is window time divided by passes |
| `sense` | T1, the four ADC conversions | cycle counter before and after, successful reads only |
| `track` | T2 from the moment the run is found due until it returns, so it includes the schedule bookkeeping and the servo update | cycle counter |
| `late` | how long after its nominal release T2 started (start jitter) | release time compared with the counter value of the pass that found it |
| `period` | real time between two starts of T2 | difference of the start times |
| `cmd` | one console command, from the end of the line until its reply is sent | cycle counter, the time of the UART transmission is included |
| `logflush` | one SD write of the log (T5) | cycle counter |
| `stow` | E1: from the falling edge on the button pin to the servo update | edge time stamped in the interrupt, see `docs/tasks.md` |

`load` is the share of the window in which the CPU ran task code: the due tracking runs, the log
flushes started by the loop and the console commands, divided by the window length.
Waiting for a UART byte inside the console poll counts as idle. Not counted: the handler of the
stow button and the printing of its `EVT` line (a few milliseconds per accepted press).
`load_pm` is in permille (one tenth of a percent) and is limited to 1000.

`count`: `runs` is the number of T2 runs in the window, `missed` the releases skipped in the
window, `rx_bytes` the bytes received by the console, `rx_errors` the UART overrun, framing and
noise events (every one of them means at least one byte is lost), `passes` the loop passes.

`rx` describes the receive buffer (section "The receive buffer"): `init` is the status of
`plat_uart_rx_start()` (0 is good), `dropped` the bytes that found the ring full, `ring_max` the
most bytes it held at one time, `errors` the overrun, framing and noise events seen by the
interrupt, `isr_n`, `isr_avg` and `isr_max` the number of interrupts and the time spent in the
handler in CPU cycles. `stats reset` and every `load` window start them again.

`mem` comes from `plat_mem_info()`: `ram` total RAM, `static` the `.data` and `.bss` sections,
`heap_used` the bytes taken with `_sbrk`, `heap_res` and `stack_res` the reserved sizes from the
linker script, `stack_used` the deepest stack found by painting, `stack_free` the bytes between
the heap break and the deepest stack word that were never touched.

## Memory: how the stack is measured

`plat_mem_init()` is the first call in `main`. It fills the RAM between the end of `.bss` and
the current stack pointer with the pattern `0xA5A5A5A5`. `plat_mem_info()` scans upward from the
heap break to the first word that is no longer the pattern; everything from there to the top of
RAM was used by the stack at some time. Limits of the method:

- the stack used before `main` (reset handler, `SystemInit`) is not painted and not counted;
- a stack word that is rewritten with exactly the pattern is counted as untouched;
- the result is a high-water mark since reset, it is not cleared by `stats reset`.

Measured: `stack_used` is 916 B in S1 and e1-idle, 924 B in S2 and 940 B in S3 (builds C and D);
`heap_used` is 0 in every run. The static worst case from `-fstack-usage` is 1016 B (864 B for the
deepest call chain, 152 B for the three nested interrupts) against the 1024 B reserve, so the
painted 940 B is 76 B below the static bound (ratio 1.08); see `docs/metrics/memory.txt`.

## The load window: an extra load for breaking-point runs

`load <us> <ms>` is a test aid, not a product function. One command does three things: it starts a
new measurement window (like `stats reset`), it makes every pass of the loop spend `<us>`
microseconds in a busy wait (the cycle counter is read until the time is over, so the wait is at
least `<us>` and longer by up to one clock reading), and it ends by itself.

- The window ends at the first pass after `<ms>` milliseconds. The firmware then prints the
  `STAT` lines of the window and, as the last line, `EVT load done us=<us> ms=<window>`. Nothing
  has to be sent while the load is on. That matters: once a pass is longer than about one byte
  time the console loses bytes, and a command to switch the load off would be lost too.
- The extra time counts as busy time, so `STAT load` and the busy pin PB6 both see it.
- A `load` command that arrives while a window is open starts a new window.
- The command also restarts the 50 ms grid of task T2: the first run of the window is due 50 ms
  after it, and the release that was pending is replaced, not counted as missed. The phase of T2
  inside the window is then known, so test bytes can be sent into the gap between two runs.
- Limits: `<us>` up to 50000, `<ms>` from 1 to 60000. There is no way to leave a window open.
- Cost while no window is open: one byte is tested in `app_poll`. Flash grew by 636 bytes
  (counted on a local build with arm-none-eabi-gcc 13.2.1, not with the 10.3.1 of the image that
  `make build` uses, so the absolute size differs).
- Smoke run `load 40 300` (build B, `docs/metrics/analyzer-load.txt`): P was 85.8 us on average
  (85.75 to 86.00 us), 0.7 us below the byte time. That is more than the 70 to 80 us predicted from
  28 + 40 us: the wait is a whole number of clock reads (8 to 9 us each) and a pass in a window
  costs about 19 us more than a plain pass even at X = 0 (P = 47.2 us in `sweep-uart-poll`, plain
  pass 28.06 us in `analyzer-stats.txt`).

## Breaking-point sweeps

Two scenarios raise the extra load per pass step by step, one `load` window per step. After the
run `tools/lr3_sweep.py` prints one row per window.

| Scenario | Load per pass | In each window | Run |
|----------|---------------|----------------|-----|
| `sweep-uart` | 0 to 100 us | four 48-byte console lines (`resume` and 40 digits) | `tools/lr3_capture.sh sweep-uart sweep 60000` |
| `sweep-ms` | 1 to 30 ms | three stow presses, 300 ms apart | `tools/lr3_capture.sh sweep-ms sweep 60000` |

- Nothing is awaited inside a window, because a reply may be lost. Each window ends with
  `wait-serial 'EVT load done us=<X> ms='`, which is always printed.
- The `load` command restarts the 50 ms grid of T2, so the runs are due 50, 100, ... ms after it and
  start up to 6 ms late. The lines of `sweep-uart` start 15 ms after the reply and are 50 ms apart.
  A line lasts 4.2 ms, so it always lies in the gap between two T2 runs.
- The control window `load 1 300` sends two double lines that start 4 ms before a T2 run. The bytes
  lost there are lost to T2 and not to the length of the pass. The column `T2 in burst` shows from
  the capture how many replies belonged to a line that overlapped a T2 run.
- X is the requested wait. The real extra time is a multiple of one clock reading (8.5 us) plus the
  loop overhead, so the tables use P measured on PB6, not X.
- First table: P, the margin to the byte time, load by the firmware and by the pin (permille),
  received and lost bytes (`rx`, `lost`), overruns (`err`), intact replies (`ok`) and damaged ones
  (`bad`). Second table: pass period, T2 lateness and missed runs against the 25 ms deadline, stow
  reaction against 10 ms (firmware and pin). The last lines give the first window that broke each
  limit and the last window that did not.

Results (both sweeps were run on build B, before the receive ring; the ring changes the first
sweep, see "The receive buffer"):

- `sweep-uart` (`docs/metrics/analyzer-sweep-uart-poll.txt`, 11 windows, 192 bytes sent in each):
  no byte lost at X = 0 (P 47.2 us on average, 53.8 us at most); 30 lost at X = 17 us (P 69.6 us),
  60 at X = 42 us, 100 at X = 100 us (P 151.5 us); 62 lost in the control window, where two lines
  overlap a T2 run. `rx_errors` stayed 0 in every window, so the firmware did not see the loss.
  The prediction written before the run (first loss at X = 42 or 51 us) was wrong, because the
  per-byte cost outside the busy pin was left out; the fitted model is in "The receive buffer".
- `sweep-ms` (`docs/metrics/analyzer-sweep-ms.txt`, 7 windows, 3 presses in each): the loop pass
  period is about X + 6 ms on average (7.07 ms at X = 1 ms, 36.48 ms at X = 30 ms) and at most
  11.0, 13.0, 15.0, 17.0, 21.0, 31.0 and 41.0 ms at X = 1, 2, 4, 6, 10, 20 and 30 ms.

  | X per pass | 1 ms | 2 ms | 4 ms | 6 ms | 10 ms | 20 ms | 30 ms |
  |---|---|---|---|---|---|---|---|
  | E1 reaction, firmware, max | 5.88 | 5.27 | 9.88 | 11.54 | 12.40 | 19.40 | 23.95 ms |
  | E1 reaction, pin, max | 5.89 | 5.28 | 9.89 | 11.55 | 12.42 | 19.41 | 23.96 ms |
  | T2 start late, max | 8.09 | 11.68 | 11.31 | 15.96 | 20.06 | 27.15 | 34.24 ms |

  E1 missed its 10 ms from X = 6 ms (last good X = 4 ms, 9.88 ms) and T2 its 25 ms from X = 20 ms
  (last good X = 10 ms); no T2 release was skipped in any window. Both break points are those that
  were predicted before the run (E1 between 4 and 6 ms, T2 at about 20 ms).

## Flood runs: where the receive buffer breaks

With the ring buffer a long pass no longer loses console bytes, so the next limit is the ring
itself: it overflows when bytes arrive faster than the loop takes them out for longer than the
ring can store them. `scenarios/flood.yaml` sends lines of 48 bytes (`resume` and 40 digits) in
one write, so that they arrive back to back at the full speed of the line (4.16 ms per line):

| Window | Stream | What it shows |
|--------|--------|---------------|
| `load X 600`, X = 0, 8, 17, 34, 68, 100 us | 30 lines, 1440 bytes, 125 ms | the loop takes one byte per pass and answers every line with a 1.8 ms reply; when 48 passes and one reply last longer than the 4.16 ms of a line, the backlog grows until the ring is full |
| `load 1 800`, called `stats-reply` | the line `stats` and 10 lines, 486 bytes | the reply to `stats` blocks the loop for about 90 ms (measured: 86.9 ms), so 22 ms of the stream fit into the ring and the rest is dropped |
| `load 2 600`, called `sd-write` | the line `sd write 16000` and 5 lines, 255 bytes | the SD write blocks the loop for 13.4 ms and the reply for 3 ms, so about 190 bytes arrive; the ring holds them (measured: the write took 16.1 ms with the receive interrupts, and 240 bytes were in the ring) |

Model before the run: one line needs 48 P + R in the loop, with P the work of a pass from
`sweep-uart` (46.5 us at X = 0 to 156.1 us at X = 100) and R about 1.76 ms (the smallest `cmd`
time of a `resume`). If this is more than 4.16 ms, the backlog grows by 48 x (1 - 4.16 ms /
(48 P + R)) bytes per line, and the ring drops what does not fit.

Notes for reading the result:

- A dropped byte can be a line end, so two lines run together and the next command is damaged
  (column `bad`). A half line can stay in the line buffer and swallow the next command, so the
  scenario sends one empty line after every window to clear it (the first run without it timed
  out in the X = 8 window).
- The table `RECEIVE BUFFER` gives the bytes that arrived (counted by the interrupt), those the
  console read, those dropped, those still in the ring at the end of the window and the highest
  ring fill. `arrived` should be 1440, 486 and 255; a smaller number would mean that the
  simulator did not deliver the whole write, and the run must not be used.
  
Result (build C, `docs/metrics/analyzer-flood.txt`, `docs/captures/lr3/flood.*`): every write
arrived whole (1440, 486 and 255 bytes), so the table can be used.

- Sweep windows: dropped 53, 134, 238, 415, 575 and 657 of 1440 bytes at X = 0, 8, 17, 34, 68 and
  100 us, with the ring full (256) in every one. The bytes taken out during the 125 ms stream are
  1440 - 256 - dropped, so the loop needed 110 us per byte at X = 0 and 237 us at X = 100 (replies
  and T2 included) while a byte arrives every 86.7 us. At X = 0 (the `load` window active, adding
  nothing) a stream of about 1200 bytes, 25 lines or 100 ms, already fills the ring. The plain
  firmware without a `load` window is faster; that was not measured.
- The model written before the run was too low in every window (it predicted 0, 0, 50 to 130,
  170 to 300, 320 to 520 and 420 to 650 bytes).
- `stats-reply`: 224 dropped, as predicted (486 - 6 - 256). The reply held the loop for 86.9 ms
  (`cmd` maximum), one T2 run was skipped (`missed` 1), T2 started 55.3 ms late against the
  25 ms deadline, and two T2 starts were 105.3 ms apart.
- `sd-write`: nothing dropped, but the ring held 240 of 256 bytes (predicted 170 to 200). The
  receive interrupts ran inside the write: it took 16.1 ms (`us=16129`) instead of 13.5 ms, and
  the command with its reply 22.5 ms, longer than the 20.8 ms of the whole stream. One more
  line would have overflowed the ring.
- Damaged lines: 28, 22, 19, 12, 11 and 9 of 30 lines were answered as `resume` at X = 0 to 100;
  the others were cut or joined, and at X = 100 the console ran fragments such as
  `eue13579135791357913579` as commands.
- T2 and E1: no T2 run was missed or late by more than 5.9 ms in the six sweep windows.

Run (about 25 s), report in `docs/metrics/analyzer-flood.txt`:

    tools/lr3_capture.sh flood flood 60000

## Cost of the instrumentation

Measuring changes the thing measured, so the cost is written down.

- **Loop pass.** The pass now stores the time stamp, increments a counter and compares the gap
  with the smallest and the largest seen. These fields are the first members of `app_t`, so the
  compiler reaches them with one instruction. Three changes pay for it: the button check returns
  before it disables interrupts when no edge is pending, `plat_time_cycles()` reads the tick
  counter directly instead of calling `HAL_GetTick()` twice, and the due part of the tracking
  task is a separate function so the common pass saves fewer registers. Counted from the
  disassembly with the Cortex-M0+ cycle table, the pass is about 12 cycles shorter than before
  (1 us); the real test is that the scenarios still lose no byte.
- **Tracking run.** Two extra clock readings around the conversion, one at the end of the run:
  about 25 us in every 50 ms (0.05 %).
- **Command.** Two clock readings per command line.
- **Memory.** Flash grew by 2.2 KB and RAM by 168 bytes when the meters were added (`app` went
  from 0x4d8 to 0x580 bytes). The flash figure was counted on a local build with
  arm-none-eabi-gcc 13.2.1, not with the 10.3.1 of the image, so it is not the size of the
  simulated firmware. In the final image (build D, `docs/metrics/memory.txt`) `app_t` is 1416 bytes,
  62 % of the static RAM.
- **Output.** The reply to `stats` was 666 characters when the meters were added, and the loop was
  busy for 82.7 ms (build A, measured on PB6, the longest pass in `docs/metrics/analyzer-stats.txt`):
  57.7 ms of UART transmission at the theoretical 86.7 us per byte and about 25 ms of formatting,
  and it does nothing else. The transmission is slower than the theory (93.9 us per byte in
  `analyzer-burst.txt`), so 666 characters take 62.5 ms and the formatting about 20 ms.
  The `STAT rx` line added about 94 characters: the reply is now 760 characters in 13 lines (S1
  log, build C), 71.4 ms of transmission at 93.9 us per byte. The busy time of about 90 ms that
  was written down as an estimate has not been read from PB6 again (this is part of the re-run
  group 1 in `docs/baseline-lab4.md`). The nearest measurement is the `cmd` maximum of the same
  reply in the `stats-reply` window of the flood (build C): 86.9 ms, with the receive interrupts
  running during it. A release of T2 that falls into this time is late by up to about the length
  of the reply, and a release is skipped when the lateness exceeds 50 ms. Measured in that window:
  one release skipped, T2 started 55.3 ms late, and two T2 starts were 105.3 ms apart (firmware
  `period` maximum). Read the statistics at the end of a measurement, not during it.

## The receive buffer

Before this change the console took one byte per loop pass with a blocking call of the HAL. The USART
holds one byte, so the next byte overwrote it whenever a pass lasted longer than a byte time
(86.7 us); the simulator does not even set the overrun flag, so `rx_errors` stayed 0 while bytes
were lost. `sweep-uart` measured the limit: bytes received = sent x 86.7 / (P + 32.1 us), and the
bytes start to go at P = 54.6 us, 21 us above the longest plain pass.

This stage moves the reception out of the loop:

- The USART2 interrupt (RXNE) copies every byte into a ring buffer of 256 bytes
  (`plat_ring.c`, one producer and one consumer, free-running 16-bit indices, no lock). The
  priority is 1, below the stow button (EXTI, priority 0), so the console cannot delay the
  interrupt that reads the stow button; it can still add its own handler time to the reaction
  of the loop.
- A byte that finds the ring full is dropped and counted (`dropped`). A byte with a framing or
  noise error is discarded; the flags are cleared, `errors` is incremented, and the next call of
  `plat_uart_read_byte()` returns `PLAT_ERR_BUS` once.
- `plat_uart_read_byte(byte, timeout_ms)` takes one byte from the ring and waits for it by
  polling the millisecond tick. The timeout is still 5 ms, so an idle pass is still about 6 ms
  and PB6 is still low while the loop waits.
- 256 bytes are 22 ms of a stream at full speed. That is more than one SD write of the log
  (13.4 ms).

Cost: RAM +288 bytes (the ring 268, five counters 20). That figure does not depend on the compiler
and is confirmed by the final image (`docs/metrics/memory.txt`: `rx_ring` 268 bytes,
`plat_uart_hal.o` 288 bytes of RAM). Flash: the final image holds `plat_ring.o` 204 bytes and
`plat_uart_hal.o` 552 bytes. The increase of the whole image by this change was +492 bytes
(text 31452 to 31944) and the handler 144 bytes, both counted on a local build with
arm-none-eabi-gcc 13.2.1, which is not the compiler of the image. The flash that is left was
written down here as about 730 bytes, from the same local build; the image built with the 10.3.1
toolchain uses 31708 of 32768 bytes (96.77 %), so **1060 bytes are free** (`memory.txt`).

The time of the handler is measured by the firmware and not by the analyzer, because the pins
cannot show a 10 us pulse inside a pass: SysTick `VAL` is read at the entry and at the exit of the
handler and the difference is added up (`isr_sum`, `isr_max`, in cycles of the 12 MHz clock). The
exception entry and return (about 15 cycles each by the core documentation) are outside it.
Measured in `sweep-uart` (192 bytes in each of 11 windows): `isr_avg` 143 to 144 cycles (11.9 us),
`isr_max` 153 to 157 cycles. The prediction written before the run was 90 to 130 cycles and was
too low. At full speed one byte arrives every 1040 cycles, so the handler takes 13.8 % of the CPU,
about 16.6 % with the entry and return added.

What changes in the measurements:

- `P` on PB6 contains the handler when a byte arrives during a pass: the longest plain pass at
  X = 17 is 88.2 us against 74.9 us before the ring (+13.3 us).
- The break point of `sweep-uart` is no longer reached. All 11 windows received 192 of 192 bytes
  with `dropped` = 0, `errors` = 0 and four intact replies, up to a longest pass of 173 us (twice
  the byte time) at X = 100. Before the ring the loss began at X = 17 and X = 100 lost 100 of 192
  bytes; the control window, where two lines overlap a T2 run, lost 62 bytes before and none now.
  `ring_max` was 1 up to X = 25, 11 to 24 from X = 34 to 100 and 33 in the control window, so at
  most 13 % of the ring was used.
- New breaking points: a stream longer than the ring during the 65 ms transmission of the
  `stats` reply or during an SD write, and a sustained flood. They need their own scenario.
  
## Service pins and the logic analyzer (the second method)

The cycle counter runs inside the firmware it measures. The service pins are watched from the
outside by the simulator's logic analyzer, whose time stamps belong to the simulation and not
to the firmware. A metric measured both ways checks the firmware counter and the pins at once.

| Pin | Analyzer | Signal | Driven by |
|---|---|---|---|
| PB6 | D0 | loop busy: low while `plat_uart_read_byte` waits for a byte, high for the rest of the pass | the UART adapter, two register writes per pass |
| PB4 | D1 | T2: high from the start of `track_due` to the time stamp that ends the `track` statistic | `app.c` through `plat_probe_set/clear` |
| PB7 | D2 | EVENT: high while the stow handler runs, from before `tracker_stow` to the time stamp that ends the `stow` latency | `app.c` through `plat_probe_set/clear` |
| PB5 | D3 | the button (input, the analyzer only listens) | the button |
| PA2 | D4 | UART TX | USART2 |
| PC6 | D5 | servo 1 PWM | TIM3 |
| PB9 | D6 | SD chip select | `plat_spi` |
| PB3 | D7 | ADC marker, high during one conversion (LR2) | `plat_adc` |

PB4, PB6 and PB7 are LR3 test aids and are not in the LR2 pin table. They are outputs only,
nothing else is connected to them. The wiring of the analyzer for all LR3 captures is
`scenarios/lr3.diagram.json`; the LR2 `diagram.json` and the LR2 captures are unchanged (there
the three pins are simply not connected).

What the analyzer gives that the counter cannot:

- **Loop pass.** Rising edge to rising edge of PB6 is one pass. The high width is the work of
  one pass outside the wait for a byte, so `P`, the number that must stay below one byte time
  (86.7 us at the real 115385 baud), is read directly instead of being inferred from lost bytes.
- **T2.** Width of the PB4 pulse and start to start of two pulses; the offsets from the ideal
  50 ms grid give the start jitter. ADC pulses inside it give the sense burst.
- **E1.** From the first falling edge on PB5 to the start and to the end of the PB7 pulse, and to
  the first servo pulse with a new width. The firmware counter cannot see the last one.

Where the two methods differ, and by how much (measured values in the table below the list):

- The PB4 pulse starts a few microseconds after the time stamp that starts `track` (the gap
  bookkeeping and the call of `plat_probe_set` come first) and ends one call after the time stamp
  that ends it, so it is a few microseconds shorter than `track`.
- The `stow` latency starts at the time stamp taken in the interrupt, after the interrupt entry,
  so the analyzer sees more: 11 to 17 us in the stow captures (idle and busy).
- Both counts of passes and runs can differ by one at the edges of the window.
- The firmware `load` counts task code only (T2, log flushes, console commands). PB6 also shows
  the work of every plain pass (28 us, about 5 permille), so the busy time of the pin is larger
  than `busy_ms`; the report prints both.

Measured (maxima; reports `docs/metrics/analyzer-stats.txt`, `analyzer-load.txt`,
`analyzer-stow.txt`, `analyzer-stow-busy.txt`; the letter is the build):

| Metric | Firmware (counter) | Logic analyzer | Analyzer minus firmware | Capture |
|---|---|---|---|---|
| T2 duration | 850 us | 844.7 us | -5.3 us | `stats` (A) |
| T2 duration | 849 us | 843.7 us | -5.3 us | `load` (B) |
| T2 start to start | 54005 us | 54000.5 us | -4.5 us | `stats` (A) |
| loop pass period | 6005 us | 6004.2 us | -0.8 us | `stats` (A) |
| T1 sense (four conversions) | 602 us | 581.5 us | -20.6 us | `stats` (A) |
| passes counted | 505 | 505 | 0 | `stats` (A); 51 against 50 in `load` (B) |
| T2 start jitter | 4007 us | 4228 us | +221 us | `stats` (A); 4008 against 4008 in `load` (B) |
| `stow` latency, loop waiting | 4666 us and 1938 us | 4678.7 us and 1954.8 us | +12.7 and +16.8 us | `stow` (A) |
| `stow` latency, loop busy (SD write) | 19020 us | 19031 us | +11.0 us | `stow-busy` (A) |
| CPU load | 17 permille (task code) | 16.8 permille (T2 pulses), 23.1 permille (PB6 high) | see below | `stats` (A) |

How the differences are explained:

- T2 duration, start to start, pass period: the PB4 pulse is 5.3 us shorter than the span of the
  counter, the other two agree within 5 us.
- T1: the counter times the whole call, the analyzer only the span from the first to the last ADC
  marker, so it reads 20.6 us less.
- Jitter: the +221 us comes from the clock scale. The firmware clock runs 74 to 96 ppm fast
  against the simulation, so over the 3 s of the `stats` capture it gains 74 ppm x 3.0 s = 221 us.
  In the 0.3 s `load` capture the two agree.
- `stow`: 11 to 17 us more on the analyzer, the interrupt entry that comes before the time stamp.
- CPU load: the pin is high for 23.1 permille and the T2 pulses take 16.8 permille; the other
  6.3 permille are the plain passes (28.06 us in every 6 ms, 4.7 permille) and the commands in the
  window (the rest, about 1.6 permille).

Cost of the pins:

- PB6: two stores per loop pass, about 11 cycles including one more saved register (0.9 us at
  12 MHz, about 1.2 us in the simulator). It is the only change in the pass, so `P` measured on
  the pin contains it.
- PB4 and PB7: two calls of about 20 cycles per T2 run or per accepted press (about 4 us per
  50 ms).
- If the pass margin is ever lost, set `PROBE_BUSY_ENABLED` to 0 in
  `firmware/platform/src/probe_pins.h`; PB4 and PB7 are only touched when a T2 run or a stow
  runs, never in a plain pass.

Run (about 20 s each), report in `docs/metrics/analyzer-<scenario>.txt`, capture and serial log
in `docs/captures/lr3/`:

    tools/lr3_capture.sh stats loop
    tools/lr3_capture.sh stow event
    tools/lr3_capture.sh stow-busy event

## Long runs without the analyzer

The long scenarios (S1, S2, S3, e1-idle, nosd-log, park) are measured by the firmware only: a VCD
of minutes would be hundreds of megabytes. `tools/lr3_run.sh <scenario> [timeout_ms]` runs a
scenario with `wokwi-cli`, keeps the serial log in `docs/captures/lr3/<name>.serial.log` and the
CLI log with real-time stamps in `<name>.cli.log`, and writes `docs/metrics/stats-<name>.txt` with
`tools/lr3_stats.py` (`tools/lr3_park.py` for `park`).

- A window is everything between a `stats reset` and the next `stats`. The comment lines
  `# window` and `# press` in the scenario file say how long each window is meant to be and what
  kind of press (`idle`, `sd`, `random`, `nominal`) each `EVT stow` line belongs to; the script reads
  them back with `--scenario`.
- Every time comes from the firmware (`STAT` lines, `EVT stow t_us`), never from the moment a line
  reaches the host.
- For each window the report gives the statistics against the deadlines of `docs/tasks.md` with a
  verdict (ok or MISSED), E1 per kind of press with average, median, p90, p99 and maximum, a 1 ms
  histogram for the `idle` kind, the latency against the press offset in an `sd write`, the button
  counters, the log counters and the records read back with `log get` (checksum).
- In S1 the scenario is pure delays, so the firmware clock can be compared with the scenario
  clock: 250024 ms for 250000 ms, +96 ppm (`docs/metrics/stats-s1-idle.txt`).
- Do not use `tools/lr3_stats.py` on the flood log: a `stats` inside a `load` window would be read
  as a window of its own. Use `tools/lr3_sweep.py` there.
- `tools/lr3_regen.sh` rebuilds all these reports from the committed logs without the simulator.

## Start-up time

The start-up milestones are read from a capture that contains the reset, with no extra pin:
`python3 tools/lr3_startup.py <capture.vcd>` (output in `docs/metrics/startup.txt`, made from
`docs/captures/lr3/burst.vcd`, build D). It uses D4 (UART TX, for the banner and the self-test
lines), D7 (ADC marker), D6 (SD chip select), D5 (servo 1 PWM), D0 (loop busy) and D1 (T2). The time
zero is the first sample of the capture, which is the reset.

| Milestone | After reset |
|---|---|
| first byte on the UART (start of the banner) | 4.8 ms |
| first ADC conversion (self-test) | 10.1 ms |
| first SD chip select low after the configuration | 18.2 ms |
| first servo 1 PWM pulse (width 1500 us) | 27.0 ms |
| self-test report finished, first wait for a byte (D0 falls) | 48.9 ms |
| first normal loop pass (D0 rises again) | 54.7 ms |
| first T2 run starts, ends (first tracking decision) | 102.7 ms, 103.5 ms |

T6 asks for the main loop within 1000 ms of the reset: met, with 54.7 ms. The start-up did not
change with the builds: the first loop pass is at 54.63 ms in `stats.vcd` (build A) and 54.66 ms
in `burst.vcd` (build D). The first T2 run starts about 100 ms after the reset and then depends on
the capture: 100.7 ms in `stats.vcd` and 102.7 ms in `burst.vcd` (the phase of the 6 ms idle
pass), and 125.1 ms in `flood.vcd`, where a `load` command came first and restarted the 50 ms
grid. The simulator cannot show a restart (there is no reset command) or a sensor that is
connected but not ready; the only device with an init sequence is the card, which was tested
absent in nosd-log.
