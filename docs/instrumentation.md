# Instrumentation: the `stats` command

LR3 measures the firmware with code that runs inside the firmware. This file says what is
measured, how, and what the measuring itself costs. All times are simulator times (Wokwi, core
clock 12 MHz), see the report for what that means.

## Commands

| Command | Reply |
|---|---|
| `stats` | eleven lines starting with `STAT`, then `CMD stats OK` |
| `stats reset` | `CMD stats-reset OK`, starts a new measurement window |
| anything else after `stats` | `CMD stats FAIL reason=usage` |
| `load <us> <ms>` | `CMD load OK us=<us> ms=<ms>`; at the end of the window the `STAT` lines and `EVT load done us=<us> ms=<window>` |
| `load` | `CMD load OK us=<last value> active=<0 or 1>` |

`stats reset` clears the statistics, the counters and the window start. It does not touch the
state of the product (`track`, `button` and `log` keep their own counters).

## Lines of the reply

```
STAT name=<n> n=<count> min_us=<min> avg_us=<avg> max_us=<max>
STAT load window_ms=<ms> busy_ms=<ms> load_pm=<permille>
STAT count runs=<n> missed=<n> rx_bytes=<n> rx_errors=<n> passes=<n>
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
- Cost while no window is open: one byte is tested in `app_poll`. Flash grew by 636 bytes.

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
- **Memory.** Flash grows by 2.2 KB, RAM by 168 bytes (`app` is 0x580 bytes, was 0x4d8).
- **Output.** The reply to `stats` is 666 characters. The loop is busy for 82.7 ms (measured on
  PB6): 57.7 ms of UART transmission and about 25 ms of formatting, and it does nothing else. A
  release of T2 that falls into this time is late by up to about 83 ms, and one release is skipped
  when the lateness exceeds 50 ms. Read the statistics at the end of a measurement, not during it.
  
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

Where the two methods are expected to differ:

- The PB4 pulse starts a few microseconds after the time stamp that starts `track` (the gap
  bookkeeping and the call of `plat_probe_set` come first) and ends one call after the time stamp
  that ends it, so it is a few microseconds shorter than `track`.
- The `stow` latency starts at the time stamp taken in the interrupt, after the interrupt entry,
  so the analyzer sees more: 11 to 17 us in the stow captures (idle and busy).
- Both counts of passes and runs can differ by one at the edges of the window.
- The firmware `load` counts task code only (T2, log flushes, console commands). PB6 also shows
  the work of every plain pass (28 us, about 5 permille), so the busy time of the pin is larger
  than `busy_ms`; the report prints both.

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
