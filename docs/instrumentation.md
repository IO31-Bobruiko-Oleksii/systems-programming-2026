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
- **Output.** The reply to `stats` is about 700 characters, 60 ms of UART transmission during
  which the loop does nothing else. A release of T2 that falls into this time is late and can be
  skipped. Read the statistics at the end of a measurement, not during it.