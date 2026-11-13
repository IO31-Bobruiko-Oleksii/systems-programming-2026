# Instrumentation: the `stats` command

LR3 measures the firmware with code that runs inside the firmware. This file says what is
measured, how, and what the measuring itself costs. All times are simulator times (Wokwi, core
clock 12 MHz), see the report for what that means.

## Commands

| Command | Reply |
|---|---|
| `stats` | twelve lines starting with `STAT`, then `CMD stats OK` |
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

## Flood runs: where the receive buffer breaks

With the ring buffer a long pass no longer loses console bytes, so the next limit is the ring
itself: it overflows when bytes arrive faster than the loop takes them out for longer than the
ring can store them. `scenarios/flood.yaml` sends lines of 48 bytes (`resume` and 40 digits) in
one write, so that they arrive back to back at the full speed of the line (4.16 ms per line):

| Window | Stream | What it shows |
|--------|--------|---------------|
| `load X 600`, X = 0, 8, 17, 34, 68, 100 us | 30 lines, 1440 bytes, 125 ms | the loop takes one byte per pass and answers every line with a 1.8 ms reply; when 48 passes and one reply last longer than the 4.16 ms of a line, the backlog grows until the ring is full |
| `load 1 800`, called `stats-reply` | the line `stats` and 10 lines, 486 bytes | the reply to `stats` blocks the loop for about 90 ms, so 22 ms of the stream fit into the ring and the rest is dropped |
| `load 2 600`, called `sd-write` | the line `sd write 16000` and 5 lines, 255 bytes | the SD write blocks the loop for 13.4 ms and the reply for 3 ms, so about 190 bytes arrive; the ring holds them |

Model before the run: one line needs 48 P + R in the loop, with P the work of a pass from
`sweep-uart` (46.5 us at X = 0 to 156.1 us at X = 100) and R about 1.76 ms (the smallest `cmd`
time of a `resume`). If this is more than 4.16 ms, the backlog grows by 48 x (1 - 4.16 ms /
(48 P + R)) bytes per line, and the ring drops what does not fit.

Notes for reading the result:

- A dropped byte can be a line end, so two lines run together and the next command is damaged
  (column `bad`). After the `stats-reply` window a half line stays in the line buffer; the scenario
  sends one empty line to clear it before the next window.
- The table `RECEIVE BUFFER` gives the bytes that arrived (counted by the interrupt), those the
  console read, those dropped, those still in the ring at the end of the window and the highest
  ring fill. `arrived` should be 1440, 486 and 255; a smaller number would mean that the
  simulator did not deliver the whole write, and the run must not be used.

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
- **Memory.** Flash grows by 2.2 KB, RAM by 168 bytes (`app` is 0x580 bytes, was 0x4d8).
- **Output.** The reply to `stats` was 666 characters. The loop was busy for 82.7 ms (measured on
  PB6): 57.7 ms of UART transmission and about 25 ms of formatting, and it does nothing else.
  The `STAT rx` line is about 80 characters, 7 ms more of transmission, so the
  reply is now about 750 characters and the busy time about 90 ms (an estimate, to be read from
  PB6 in the next `stats` capture). A
  release of T2 that falls into this time is late by up to about 83 ms, and one release is skipped
  when the lateness exceeds 50 ms. Read the statistics at the end of a measurement, not during it.
  
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

Cost, counted from the build: flash +492 bytes (text 31452 to 31944), RAM +288 bytes (the ring
268, five counters 20), so about 730 bytes of the 32 KB are left. The handler is 144 bytes.

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
