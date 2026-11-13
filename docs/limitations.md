# Limitations of the super-loop architecture (LR3 registry)

This file lists the limits of the bare-metal super loop that were met on this product, each with
a measurement, the reason in the architecture, a workaround that needs no OS, what an OS would
give and cost, and a hypothesis for LR4. Defects found on the way (a bug in the code, not a limit
of the architecture) were fixed and are in the problem log of `docs/lab3-report.md`.

All times are simulator times (Wokwi, 12 MHz core clock). Judging is by the **maximum**, as in
`docs/tasks.md`. Task names (T1..T6, E1) and deadlines are from `docs/tasks.md`; the way every
number was measured is in `docs/instrumentation.md`. Files are linked next to each number.

**Builds.** The firmware changed while the measurements were being made, and its banner has no git
hash. The build of each result is reconstructed from the commit in which the capture was committed
and is named A to D in `docs/baseline-lab4.md`, section 2. A result taken on build A or B carries
its letter in brackets, for example "(A)". A result without a letter was taken on C or D; these two
differ only in how a second press inside the 200 ms lockout is counted, not in the timing of any
task. A and B have no receive interrupt and ring: their loop-pass figures lack the receive handler
(about 13 us per byte received during a pass), and the reply of `stats` lacks the `STAT rx` line
(about 7 ms of transmission). The pass of the `stats` reply, 82.7 ms (A), is therefore expected
near 90 ms on the final build (the command maximum of the same reply on build C was 86.9 ms). The
captures of A are `stats`, `stow` and `stow-busy`; those of B are `load`, `sweep-ms` and
`sweep-uart-poll`. A re-run of them on the final build is optional (baseline, section 2); until
then the letters are the honest label.

## Summary

| # | Limitation | Category | Affects | Worst measured | Hypothesis for LR4 (metric: baseline -> expected) |
|---|---|---|---|---|---|
| 1 | The event reaction waits for the longest blocking section | reaction time | E1 (FR-06) | S3: 18.5 ms against 10 ms, 10 of 27 SD-phase presses late | E1 max in S3: 18.5 ms -> at most 1 ms; idle 6.1 ms -> at most 1 ms |
| 2 | Start jitter of the periodic task follows every other task's longest section | jitter | T2 (FR-02..04) | late max 5.9 / 10.5 / 21.0 ms (S1 / S2 / S3) against 25 ms | T2 late max in all scenarios: at most 2 ms; skipped runs in the `stats` window 1 -> 0 |
| 3 | Console replies and long commands block the loop (polled TX) | long operations | T4 (FR-07), T2, E1 | reply of line 9 of a burst ends at 101.9 ms; `stats` pass 82.7 ms (A); `time` 342 ms | lines of a burst answered in time 8 -> about 10 (limited by the wire); T2 late during `stats` 55.3 ms -> at most 2 ms |
| 4 | Input is lost when the loop is blocked longer than the 256-byte ring holds | overload | T4 (FR-07) | 53..657 of 1440 bytes lost in the flood; 224 of 486 during a `stats` reply | bytes lost: 0 in the flood windows, at most about 30 in the `stats` window |
| 5 | Every datum shared with an interrupt needs hand-made protection | shared resources | E1, T4 | S2: 4 presses counted as 5 (fixed, commit 19b0119) | 0 anomalies in at least 100 presses; ISR-to-task latency measured |
| 6 | A failing device blocks the loop and fails silently | failure handling | T5 (FR-05), all | card absent: 90 % of the log events dropped, loop blocked 4 ms once per second; stuck device: 0.1 s .. 4.9 s (computed) | card absent: T2 late max and E1 unaffected (at most 2 ms) |
| 7 | Adding a task changes the timing of all existing tasks | modularity | all | E1 breaks at +6 ms per pass, T2 at +20 ms per pass (B) | break points not reached up to +30 ms with the new work at the lowest priority |
| 8 | Flash is nearly full and one stack is sized by the deepest path of any task | memory | build | flash 96.77 % (1060 B free); stack worst case 1016 B of a 1024 B budget | kernel 5..10 KB must be found by trimming; RAM 31 % -> about 40..55 % (estimate) |

The most serious one is limitation 1, see the end of the file.

## How the numbers relate (worst-case model)

The loop runs one pass at a time and never interrupts a pass: `app_poll` calls `button_poll`,
`track_poll`, `load_poll` and `console_poll` in that order. A task can start only between two
passes. So for an event handled in `button_poll`:

    reaction(max)  =  B + H

where B is the longest stretch in which the loop does not return to `button_poll`, and H is the
handler time (0.10..0.12 ms: the EVENT pulse is 101.9 us wide in `stow-busy` (A), 102.2 and 119.0 us in
`stow` (A); [analyzer-stow-busy.txt](metrics/analyzer-stow-busy.txt), [analyzer-stow.txt](metrics/analyzer-stow.txt)).
The same B bounds how late T2 can start. B, taken from the measurements:

| Source of B | Length | Where measured |
|---|---|---|
| idle pass (5 ms wait for a byte plus tick alignment) | 6.0 ms | [stats-s1-idle.txt](metrics/stats-s1-idle.txt), loop pass max 6007 us |
| console command with its reply, typical | 11.8 ms (S2), 19.9 ms (S3) | [stats-s2-nominal.txt](metrics/stats-s2-nominal.txt), [stats-s3-worst.txt](metrics/stats-s3-worst.txt) |
| `sd write` command (13.4 ms write, 3.6 ms reply, parsing) | about 21 ms | [stats-s3-worst.txt](metrics/stats-s3-worst.txt), table "press during an sd write" |
| `stats` reply | 82.7 ms (A) | [analyzer-stats.txt](metrics/analyzer-stats.txt), longer pass without a T2 run |
| `time` command (diagnostic, 40000 clock reads) | 342 ms | [stats-e1-idle.txt](metrics/stats-e1-idle.txt), window W2 |

Check against the data: idle B = 6.0 ms gives 6.1 ms, measured max 6.113 ms (n = 40); S3 B = 21 ms
and the earliest press 3 ms after the line gives 18 ms, measured 18.5 ms.

## Registry

### Limitation 1: The event reaction waits for the longest blocking section

**Symptom.** The stow request E1 (pushbutton, extension of FR-06) is late when it arrives during a
long operation. Deadline: servo width updated within 10 ms of the edge, and the pulse on the servo
output changed within 30 ms.

**Evidence.**
- S3, 30 presses: max 18.5 ms, avg 7.8 ms, verdict MISSED. Presses that came during an `sd write`:
  27, of which 10 took more than 10 ms; the 3 random presses: max 5.0 ms
  ([stats-s3-worst.txt](metrics/stats-s3-worst.txt)).
- The reaction depends on when the press came: a press `d` ms after the `sd write` line was sent
  waits for the rest of the command, about 21 ms minus `d` (3 ms: 18.0..18.5 ms; 11 ms: 10.0 ms;
  19 ms: 1.7 ms). After about 21 ms only the idle-pass phase is left (0.6..5.6 ms).
- One press on the logic analyzer (build A, [stow-busy.vcd](captures/lr3/stow-busy.vcd),
  [analyzer-stow-busy.txt](metrics/analyzer-stow-busy.txt)): the edge came 712 us after the loop
  became busy, the busy stretch lasted 24.8 ms, the EVENT pulse started 18.93 ms after the edge, the
  firmware printed `lat_us=19020` (the analyzer is 11.0 us above the firmware), and the new servo
  pulse started 31.37 ms after the edge: both deadlines missed (the second by 1.4 ms).
- The log flush blocks the same way without any command: a 512-byte block write takes up to 13.5 ms
  (80 flushes in S2) and 13.8 ms (28 in S3; `flush_max_us` in the two reports). A press that arrives
  during one waits that long, so even S2 could miss the deadline (about 0.8 % per press: 80 flushes of
  13.5 ms in 129 s). It did not in S2, by chance.
- Idle floor: 40 presses in an idle system ([stats-e1-idle.txt](metrics/stats-e1-idle.txt)): min 0.34 ms,
  median 2.69 ms, p90 5.51 ms, max 6.11 ms, 40 of 40 accepted. This meets 10 ms by 3.9 ms.
- Per-pass work that is added to every pass (build B, [analyzer-sweep-ms.txt](metrics/analyzer-sweep-ms.txt)):
  firmware reaction 9.9 ms at +4 ms per pass (met), 11.5 ms at +6 ms (missed).

**Cause in the architecture.** The ISR (`EXTI4_15_IRQHandler`) only counts the edge and stamps the
time. The work (`tracker_stow`, `apply_servos`, the report line) is done by `button_poll`, which runs
between passes. With one thread and no preemption, an urgent task cannot interrupt a non-urgent one,
so the reaction is the remaining length of whatever the loop is doing. This is not a coding mistake:
the `sd write` is correct and bounded, but it holds the only thread for 13.4 ms and its reply for
3.6 ms more.
The idle floor of 6 ms has a second cause that is a design choice of this firmware, not of the
architecture: the 5 ms wait for a byte (`plat_uart_read_byte`) does not return when the button flag is
set.

**Workaround without an OS.**
- Do the stow itself in the EXTI handler: `tracker_stow` and the servo register writes, leaving only
  the report line to the loop. Reaction becomes the interrupt entry plus the handler, about 0.12 ms,
  whatever the loop does. Cost: the tracker state (angles, `stowed`, `parked`) and the servo
  registers are now written by an interrupt, by T2 and by the console commands `deadband`, `zero`,
  `stow` and `resume`, so every
  writer needs a critical section (the T2 body is 0.85..1.15 ms, so E1 would wait at most that long
  if the critical section covers the body), or the tracker must take a "stow requested" flag. The
  200 ms lockout moves into the handler. The host tests call functions one after another and cannot
  find an interleaving bug (see limitation 5).
- Cut the long sections into steps (a state machine for `sd write`: command, 16 chunk calls, busy
  polling, each call about 0.1 ms according to [analysis.md](captures/analysis.md)). Cost: the SD
  driver, today five straight retry loops, becomes states with counters; the card's own busy time
  must be polled without waiting.
- Make the 5 ms wait return on the button flag (one condition). Cost: one more variable read per
  poll. This removes the 6 ms idle floor only; it is not applied in the measured build.
- The end-to-end pulse cannot be better than the PWM frame: the new width appears at the next 20 ms
  frame, 8.7 ms after the handler in the idle `stow` capture. No architecture removes that.

**What an OS would give.** Preemptive priority scheduling: the EXTI handler gives a task
notification, a high-priority E1 task runs as soon as the handler returns, even if the SD write is in
progress in a lower-priority task. Blocking calls in other tasks no longer decide the reaction.

**What an OS would cost.** A context switch and the notification path on every press (not measured
yet); RAM for the task (control block 64 B plus its stack) and the tick interrupt. New error classes:
the E1 task and the T2 task share the tracker and servo state, so a mutex or a queue is needed; if E1
waits for a mutex that a lower-priority task holds, the reaction is again as long as that task's
critical section (priority inversion, solved only by priority inheritance). The Cortex-M0+ has no
BASEPRI register, so RTOS critical sections mask all interrupts, and the E1 interrupt latency then
includes the longest of them. Stack overflow of the new task.

**Hypothesis for LR4.** Same S3 scenario and the same 30 presses: E1 max from 18.5 ms to at most
1 ms (handler 0.12 ms plus switch plus the longest masked section), and independent of `d`;
idle max from 6.1 ms to at most 1 ms. The end-to-end pulse in `stow-busy` (A) from 31.4 ms to at most
about 21 ms (reaction plus one 20 ms frame), which meets the 30 ms deadline.

### Limitation 2: Start jitter of the periodic task follows every other task's longest section

**Symptom.** T2 (tracking, FR-02..04) starts later and later after its nominal time as the other
tasks get busier. The deadline is 25 ms from the nominal start.

**Evidence.**

| Scenario | T2 runs | T2 start late, max | Period, max | Loop pass, max |
|---|---|---|---|---|
| S1 idle | 5000 | 5.86 ms | 54.0 ms | 6.0 ms |
| S2 nominal | 2582 | 10.54 ms | 59.7 ms | 20.0 ms |
| S3 worst | 946 | 20.96 ms | 66.6 ms | 24.6 ms |

Source: [S1](metrics/stats-s1-idle.txt), [S2](metrics/stats-s2-nominal.txt),
[S3](metrics/stats-s3-worst.txt). No run was skipped in any of them, but the margin to 25 ms in S3 is
4 ms. A `stats` reply is longer than the period: in the flood run the window with a `stats` reply had
T2 late max 55.3 ms and one skipped run ([analyzer-flood.txt](metrics/analyzer-flood.txt), window
`stats-reply`). With per-pass work added, the first miss is at +20 ms per pass (+10 ms is the last good
value; build B, [analyzer-sweep-ms.txt](metrics/analyzer-sweep-ms.txt)).
Two methods for the same metric (build A, [analyzer-stats.txt](metrics/analyzer-stats.txt)): T2 start jitter
4007 us by the cycle counter and 4228 us on the analyzer (+221 us, about 74 ppm of clock difference
between the firmware time base and the simulation; see "Not counted" below).

**Cause in the architecture.** T2 is time-triggered inside the loop, so `track_poll` can start it only
when the pass in progress ends. Late max is therefore the longest pass of any other task (B in the
model above). `track_due` keeps an absolute grid (`track_next`), so lateness does not accumulate, but
a run that is more than one period late is counted as skipped (`track_missed`).
The 6 ms floor in S1 (late max 5.86 ms, avg 3.86 ms) is the idle pass and a design choice (see
limitation 1); the 21 ms in S3 is the architecture.

**Workaround without an OS.**
- Start T2 from a timer interrupt and keep the loop for the rest. Jitter would drop to the interrupt
  latency. Cost: the T2 body (0.83..1.15 ms) runs in an interrupt. Its ADC read calls
  `HAL_ADC_PollForConversion` with a timeout, and that timeout counts `HAL_GetTick`, which is advanced
  by SysTick at priority 3: if the T2 interrupt has a higher priority than SysTick, the tick never
  advances inside it and the timeout cannot expire. So the ADC read would have to be rewritten
  without HAL timeouts, and the tracker state becomes shared with the commands (limitation 5).
- Shorten the other tasks' sections (limitations 1 and 3).
- Replace the polled wait with `__WFI()` so an idle pass ends on the next interrupt (idle jitter would
  fall to about one SysTick period, 1 ms). Not applied in the measured build.

**What an OS would give.** T2 as a high-priority periodic task with an absolute delay
(`vTaskDelayUntil`, the same idea as `track_next`) that preempts the console and logger tasks.
Jitter becomes tick quantization plus preemption latency.

**What an OS would cost.** Start times are quantized to the tick (1 ms at 1 kHz, an assumption for
LR4). The priority assignment has to be justified (rate-monotonic order: E1, T2, console, logger).
The commands that change `deadband`, `zero`, `stow` and `resume` touch the tracker used by T2: they
become messages to T2 or need a mutex. A tick interrupt every millisecond (the firmware already has
one: SysTick) with a longer handler.

**Hypothesis for LR4.** T2 late max at most 2 ms in S1, S2 and S3 (from 5.9 / 10.5 / 21.0 ms);
no skipped run in the `stats` window (from 1 and 55.3 ms); in the per-pass-work sweep the first miss
is not reached up to +30 ms when the added work runs as the lowest-priority task.

### Limitation 3: Console replies and long commands block the loop (polled TX)

**Symptom.** A command is executed and answered inside the loop. While the reply is being sent byte
by byte, nothing else runs. T4 (FR-07) wants the reply within 100 ms of the line end; T2 and E1 are
delayed by every reply.

**Evidence.**
- One `track` reply (100 bytes) ends 12.1 ms after the line end, and each further queued line adds
  11.74 ms (8.667 ms of wire time, 0.75 ms of gaps between bytes, 2.33 ms between replies).
  In a burst of 20 `track` lines sent back to back, the reply of line 8 ends at 90.9 ms (met), line 9 at
  101.9 ms (missed); 12 of 20 replies are late and the last one ends at 225.9 ms
  ([analyzer-burst.txt](metrics/analyzer-burst.txt), [burst.vcd](captures/lr3/burst.vcd)).
- The wire byte takes 93.9 us instead of 86.7 us (about 8 % more): the simulated USART seems to start the
  next byte only after the data register is refilled (cause not confirmed).
- `stats` blocks the loop for 82.7 ms in one pass (build A, [analyzer-stats.txt](metrics/analyzer-stats.txt));
  on the final build, with the `STAT rx` line, about 90 ms is expected;
  the flood run with a 12-line `stats` shows a period max of 87.8 ms and T2 late 55.3 ms.
- `time` (a diagnostic command with 40000 clock reads) blocks the loop for 342 ms: 18 T2 periods
  skipped in a 1032 ms window, T2 late max 318 ms, CPU load 998 per mille
  ([stats-e1-idle.txt](metrics/stats-e1-idle.txt), window W2). It is a diagnostic, but it is the
  measured worst case for "a long operation in the command interpreter".
- Even a report line costs: each accepted press prints a 35-byte `EVT stow` line (3.3 ms). T2 late max
  in the idle run with 40 presses is 10.2 ms against 5.9 ms in S1; the difference is of the size of one
  such line (consistent, not isolated by a separate run).

**Cause in the architecture.** The product has no queue between "decide the reply" and "send it". `say`
formats and calls the blocking `HAL_UART_Transmit` (timeout 1000 ms), so the wire speed becomes loop
time. Commands run to completion inside `console_poll`.

**Workaround without an OS.** An interrupt-driven transmitter with a ring buffer (the mirror of the
receive ring): `say` formats into the ring and returns; the TXE interrupt sends. Or DMA. Cost: RAM for
the ring (256 B is 3 % of the 8456 B that are free), a second source in the USART2 handler, a policy
when the ring is full (drop or wait), and the order of `EVT` lines against `CMD` replies must still be
kept. It does not remove the wire limit: 100 bytes take 9.4 ms, so at most 10 replies fit in 100 ms
(100 / 9.4 = 10.6). Shorter replies help more than any buffer. Long commands such as `stats` would
still have to be cut into one line per pass.

**What an OS would give.** A console task at low priority with a TX queue or stream buffer. The reply
is queued and the task waits on the UART; T2 and E1 preempt it. Commands may take long without
affecting the others.

**What an OS would cost.** Queue storage (a queue is 76 B plus its storage) and a console stack of at
least the 864 B that the deepest chain needs today ([memory.txt](metrics/memory.txt)). Several tasks
that print (E1 report, logger error, console) need one owner of the UART, or a mutex, otherwise lines
interleave; a mutex held by a low-priority printing task while a higher one waits is priority
inversion.

**Hypothesis for LR4.** Burst of 20 `track` lines: lines answered within 100 ms from 8 to about 10
(the wire limit, not the OS), and during the burst T2 start to start close to 50 ms (now 59.3 ms, then 47.1 ms three times, from
the 5 runs inside the burst). Window with a `stats` reply: T2 late 55.3 ms to at most
2 ms, skipped runs 1 to 0. `time`: skipped T2 runs 18 to 0.

### Limitation 4: Input is lost when the loop is blocked longer than the 256-byte ring holds

**Symptom.** Console bytes are lost during a flood or when a long operation runs while the host keeps
sending. A line with a lost byte is a damaged command; a lost line end merges two lines.

**Evidence.**
- Before the interrupt and ring (build B, [analyzer-sweep-uart-poll.txt](metrics/analyzer-sweep-uart-poll.txt)):
  the loop must take a byte every 86.7 us. The first loss came at +17 us of work per pass (P avg 69.6 us;
  the last good window had P avg 47.2 us), and 100 of 192 bytes were lost at +100 us. A fit of the
  ten windows puts the break point at about 54.6 us of work per pass (a model, not a measurement).
- After it (USART2 interrupt, 256-byte ring, commit 42f6691), the same sweep loses nothing
  ([analyzer-sweep-uart.txt](metrics/analyzer-sweep-uart.txt)): 192 of 192 bytes in all 11 windows.
- Remaining break point: the ring holds 256 bytes, which is 22.2 ms at line rate. A flood of 1440 bytes
  (125 ms at line rate) loses 53 bytes with no extra work per pass and up to 657 with 100 us per pass;
  during a 12-line `stats` reply 224 of 486 bytes are lost; during an `sd write` window 0 of 255 are lost,
  but the ring reached 240 of 256 ([analyzer-flood.txt](metrics/analyzer-flood.txt),
  [flood.serial.log](captures/lr3/flood.serial.log)).

  | extra work per pass | 0 | 8 | 17 | 34 | 68 | 100 us |
  |---|---|---|---|---|---|---|
  | bytes lost of 1440 | 53 | 134 | 238 | 415 | 575 | 657 |

- Damaged lines reach the interpreter: the flood log has 14 lines `CMD ? FAIL reason=too-long` and
  commands such as `rsume...` and `eue1357...` that are answered `FAIL reason=unknown`. A damaged
  line that still parses as a valid command would be executed. All damaged lines in this log were
  rejected, but nothing in the protocol prevents the other case.
- S3 stays safe: 1067 bytes in, 0 dropped, ring max 114 of 256 ([stats-s3-worst.txt](metrics/stats-s3-worst.txt)).
- The firmware cannot see losses of the hardware register: `errors` stayed 0 although up to 100 of
  192 bytes were lost before the ring, because the simulator does not set the overrun flag. After the
  ring the `dropped` counter shows ring overflow. The handler code that clears ORE/FE/NE has never
  run in a test; the application's reaction to a reported bus error is tested on the host with the fake UART.

**Cause in the architecture.** Reading the UART needs the loop to run. The ring turns the limit from
"one pass" into "the longest blocking section", but it is still a limit: B above 22.2 ms at line
rate loses bytes, and the longest sections (`stats` 82.7 ms (A), `time` 342 ms) are longer than that.

**Workaround without an OS.** Bigger ring (RAM is free: each 256 B adds 22.2 ms); DMA in circular mode
with an idle-line interrupt, which also removes the 143-cycle (11.9 us) interrupt per byte, 13.8 % of the
CPU at line rate; a framed protocol with length and checksum so a damaged line is rejected; and
cutting the long sections (limitations 1 and 3). The ring and interrupt already cost about 0.5 KB of
flash and 0.3 KB of RAM (measured with another toolchain build than the final one, so approximate).

**What an OS would give.** A receive path whose consumer is guaranteed to run soon after a byte
arrives (a high-priority task, or an interrupt-to-queue hand-off), so the buffer only has to cover a
scheduling latency and not the longest blocking section.

**What an OS would cost.** A queue call from the interrupt for every byte costs more than the bare
ring (the current handler: 143 cycles); a stream buffer with one notification per line is cheaper.
Bytes can still be lost if a higher-priority task keeps the processor. If the single console task
also does the formatting, the 20..25 ms of formatting in `stats` (the 82.7 ms pass (A) minus the transmit
time of its 666 bytes, 62.5 ms at 93.9 us or 57.7 ms at 86.7 us) is as long as the ring.

**Hypothesis for LR4.** Same flood scenario: bytes lost 0 in the six sweep windows (from 53..657) and the
`sd write` window; at most about 30 in the `stats` window (from 224), because the formatting alone
(about 20..25 ms) is as long as the ring holds unless receiving is a separate task. Interrupt
cost per byte: measured, no baseline value to promise.

### Limitation 5: Every datum shared with an interrupt needs hand-made protection

**Symptom.** S2: 4 presses were sent and 5 were accepted: the 200 ms lockout was bypassed.

**Evidence.**
- The two `EVT stow` lines of that run had time stamps that went backwards by 724 us
  ([stats-s2-nominal.txt](metrics/stats-s2-nominal.txt): "accepted 5, ignored 414, presses sent: 4";
  [s2-nominal.serial.log](captures/lr3/s2-nominal.serial.log)). `(uint32_t)(first - last)` wrapped to a huge
  number, so the lockout test passed. Cause not proven: the stamp was probably taken in the priority-0
  EXTI handler while the priority-3 SysTick, which counts the milliseconds, was still pending
  (possibly an artifact of the simulator). Fix with a 2 ms tolerance: commit
  [19b0119](https://github.com/IO31-Bobruiko-Oleksii/systems-programming-2026/commit/19b0119).
  After it, 40 of 40 presses were accepted in the idle run ([stats-e1-idle.txt](metrics/stats-e1-idle.txt)).
  The committed S2 report still shows the 5-of-4 result, as the record of the problem.
- Three places in the code where `volatile` alone is not enough, and what was added:
  `plat_button_take` reads the pair `edge_count` / `edge_first`, which must be consistent, so it
  switches interrupts off for those few instructions; `plat_time_cycles` combines the tick counter,
  the pending flag and the counter value and repeats the read until they agree;
  `plat_uart_rx_reset_stats` clears the ring and interrupt statistics with interrupts off, because
  the handler increments them.
- Three interrupt priorities are assigned by hand: EXTI 0, USART2 1, SysTick 3. The nesting adds
  152 B to the worst-case stack ([memory.txt](metrics/memory.txt)).

**Cause in the architecture.** In a super loop the tasks cannot race with each other, because they
never interrupt each other; the only concurrency is the boundary between the loop and the interrupts.
Every datum on that boundary has to be protected by hand, and every function moved into an interrupt
as a workaround for limitations 1..4 enlarges the boundary.

**Workaround without an OS.** Keep the boundary small: single-writer variables, one single-producer
single-consumer ring, interrupt-off sections of a few instructions, a table of priorities and who may
call what. Do not call HAL functions with a timeout in an interrupt (they wait for SysTick). Cost:
review effort; host tests are sequential and cannot reproduce an interleaving, so this class was found
only in a simulator run.

**What an OS would give.** A sanctioned way to cross the boundary (queues and notifications with
`FromISR` calls, mutexes), one time base (the tick count), and checks (assertions on wrong API use).

**What an OS would cost.** It does not remove the time-stamp problem above: a tick count read in a
higher-priority handler has the same granularity. New error classes: priority inversion on a shared
mutex, deadlock between two mutexes (tracker and UART), using a non-`FromISR` call in an interrupt,
and a priority limit above which handlers may not call the kernel. On the Cortex-M0+ critical sections
mask all interrupts.

**Hypothesis for LR4.** `accepted` equals the number of presses and there are no backward time stamps
in at least 100 presses over the e1-idle and S3 scenarios (now 40 of 40 in the idle run, which ran after
the fix; S3 ran before it and had no duplicate: 30 of 30). The notification
latency from the EXTI handler to the first instruction of the E1 task is measured on the existing
EVENT pin; there is no LR3 baseline for it.

### Limitation 6: A failing device blocks the loop and fails silently

**Symptom.** With the SD card absent, FR-05 (log) silently stops working; the product keeps running,
and the only signs are two counters in the reply of `log`. A device that answers but never becomes
ready would block the loop for seconds.

**Evidence (measured, card absent).** Scenario [nosd-log](../scenarios/nosd-log.yaml): MISO wire
cut, `log on`, tracker moving, 12 s ([stats-nosd-log.txt](metrics/stats-nosd-log.txt),
[nosd-log.serial.log](captures/lr3/nosd-log.serial.log)):
- `log`: seq 16, stored 0, blocks 0, dropped 144, errors 12. The log kept 16 records and dropped the
  other 144 of 160 events (90 %), one failed flush per second (`LOGGER_RETRY_MS` = 1000, counted from the start of the attempt).
- `sd write` fails with `step=init status=3` in 7.868 ms each, of which the 38-byte reply is 3.6 ms;
  a failed init is therefore about 4 ms (3.6..4.3 ms by two derivations in the same report).
- T2: 240 runs, 0 skipped, late max 5.89 ms (S1: 5.86 ms), because `log_poll` runs after the T2 body.
  So the failure does not delay T2, but the loop is blocked about 4 ms once per second, and an E1
  press at that moment waits that long (derived, no press in this run).

**Evidence (computed, device present but not answering).** The simulator's card cannot be made to stall,
so these are bounds, computed from the constants in `sdcard.h` and `plat_spi_hal.c` and the per-call
timing measured in LR2 (about 99 us between SPI calls, 6-byte command 128 us at 375 kHz;
[analysis.md](captures/analysis.md)):

| Blocking call | Normal | Bound if the device does not answer | Basis |
|---|---|---|---|
| UART receive wait | 5 ms (by design) | the same | measured |
| UART transmit, 100-byte reply | 9.4 ms | 1000 ms (`TX_TIMEOUT_MS`) | measured / constant |
| ADC read, 4 channels (T1) | 0.58..0.60 ms | 100 ms: the T2 run stops at the first failed channel (`ADC_POLL_TIMEOUT_MS`) | measured / constant |
| SD init, card absent | about 4 ms | the same | measured |
| SD init, card stays idle (ACMD41 loop, 1000 tries) | about 4.5 ms (card ready; LR2: 1.1 ms of power-up bytes plus 3.39 ms for the five commands) | 0.69 s (answer at the first poll) .. 2.4 s (answer at the eighth) | computed |
| SD write, card stays busy (5000 polls) | 13.4 ms | about 0.49 s per flush attempt | computed |
| SD read, no start token (50000 polls) | 10.5 ms (LR2) | about 4.9 s (console `sd read` and `log get` only) | computed |

Every loop is bounded, so there is no endless wait. But a stuck card during a flush holds the loop for 0.49 s
(about 10 T2 periods, and an E1 press would wait as long), and a failed init is retried 1 s after it
started (`logger_failed` takes the time stamp of the start of the T2 run, so the retry delay includes the
time the failed attempt itself used). An init that takes 0.69 s therefore blocks the loop 66..69 % of
the time, and one that takes 1 s or more (up to 2.4 s) blocks it all the time: the loop gets at most one T2 period
(50 ms) between two attempts, so console and E1 would be served only in those short gaps. There is no watchdog in the
firmware.

**Cause in the architecture.** Drivers wait in loops inside the only thread; nothing supervises the
loop from outside; the error is handled where it happens (`logger_failed`), with no way to tell another
task about it.

**Workaround without an OS.** Bound the time instead of the count (give up after a deadline measured
with the tick), raise one visible event on the first failure (`EVT log FAIL` and a counter in the
`track` reply), apply a hardware watchdog (IWDG) around the loop, and make the SD driver a state machine
so no single call holds the loop (as in limitation 1). Cost: more states, an IWDG that must be fed from
the loop and so cannot detect a task that is only slow.

**What an OS would give.** The SD transfer in interrupt or DMA mode with a semaphore wait with a
timeout, so a stuck card blocks only the logger task; a per-task watchdog (each task reports in).

**What an OS would cost.** An interrupt-mode SPI driver; the logger's retry loop still uses CPU at the
logger's priority (so it has to be the lowest). The SPI bus is shared by the logger and the console
`sd read`: a mutex, with the inversion risk of limitation 5. The loss of log records does not change.

**Hypothesis for LR4.** Same nosd-log scenario: T2 late max and E1 reaction unaffected by the failed
init (at most 2 ms, no 4 ms hole once per second); `dropped` stays 144 (the data loss is not an OS
problem). The computed stuck-card bounds cannot be checked in the simulator.

### Limitation 7: Adding a task changes the timing of all existing tasks

**Symptom.** A new activity cannot be added without checking every deadline again.

**Evidence.** The `load <us>` command adds work to every pass, which is the worst case of a new task
whose longest section is X. Seven values of X were run
(build B, [analyzer-sweep-ms.txt](metrics/analyzer-sweep-ms.txt), [sweep-ms.vcd](captures/lr3/sweep-ms.vcd)):

| X per pass | 1 ms | 2 ms | 4 ms | 6 ms | 10 ms | 20 ms | 30 ms |
|---|---|---|---|---|---|---|---|
| E1 firmware reaction, max | 5.9 | 5.3 | 9.9 | 11.5 | 12.4 | 19.4 | 23.9 ms |
| E1 verdict (10 ms) | ok | ok | ok | MISSED | MISSED | MISSED | MISSED |
| T2 late, max | 8.1 | 11.7 | 11.3 | 16.0 | 20.1 | 27.2 | 34.2 ms |
| T2 verdict (25 ms) | ok | ok | ok | ok | ok | MISSED | MISSED |

So any new task with a section longer than about 4 ms breaks E1, and longer than about 10 ms breaks T2,
however rarely it runs: the worst case is the longest section, not the average load. Code structure:
all tasks, the command interpreter, the meters and the report are in one file, `app.c`, 1057 lines,
6031 B of flash (the largest object of the product), and its state `app_t` is 1416 B, 62 % of the static
RAM ([memory.txt](metrics/memory.txt)). A new periodic task needs at least four places: a member in
`app_t`, its initialisation, a call in `app_poll` (the position is the priority), and its metric in
`meter_reset` and `print_stats`. Counter-example: T3 (park) lives inside T2 as a function of the tracker
and needed no scheduling code; its cost showed up as time inside T2 (body max 0.83 ms in S1, 1.15 ms
in S2 and S3).

**Cause in the architecture.** Timing is not composable: with one thread and no preemption, the
latency of each task is bounded by the longest section of all the others. There is no isolation, so the
bound has to be re-derived for the whole product whenever one task changes.

**Workaround without an OS.** Give every task a time budget written in `docs/tasks.md` (today about
4 ms for E1 and 10 ms for T2), split long work into steps (state machines), and use the `stats`
maxima as the check after every change. Cost: state machines make straight code longer; the budget
table has to be kept up to date by hand.

**What an OS would give.** Independent tasks, each in straight-line code with its own stack; priorities
decide who delays whom, so a low-priority task does not change E1 or T2 beyond the switch cost;
blocking calls with timeouts.

**What an OS would cost.** A stack per task, to be sized and watched (overflow is silent corruption);
every shared object needs a mutex or queue; a priority assignment that has to be proved; the
configuration (`FreeRTOSConfig.h`) and the tick interrupt.

**Hypothesis for LR4.** The same sweep with the added work as the lowest-priority task: neither E1 nor
T2 breaks up to X = 30 ms (now at 6 ms and 20 ms). Places to edit for one new periodic task: at least
4 now, 2 expected (the task function and its creation), to be counted on the same example.

### Limitation 8: Flash is nearly full and one stack is sized by the deepest path of any task

**Symptom.** There is little room to add code, and the stack reserve has only an 8 B margin against
its static worst case.

**Evidence** ([memory.txt](metrics/memory.txt), Docker build, arm-none-eabi-gcc 10.3.1):
- Flash 31708 of 32768 B (96.77 %), 1060 B free. By group: our code 12355 B, HAL and start-up
  10589 B, newlib-nano 4212 B, string constants 3333 B, libgcc 936 B. The printf family is about
  2.6 KB (`vfprintf_i` 1096, `svfprintf` 1064, `snprintf` 256, `vsnprintf` 220) and the 64-bit division
  helper `_udivmoddi4` 416 B.
- RAM 3832 B (31.18 %) with the reserve (static 2296 B, heap 512 B, stack 1024 B); 8456 B never used.
  `STAT mem static` in the simulator is 2296 B too, so the simulator runs this build.
- Stack: the deepest call chain is 864 B (`main`, `app_poll`, `console_poll`, `run_line`, `app_execute`,
  `cmd_stats`, `print_stats`, `print_loop`, `print_stat`, `say`, `vsniprintf`, `_svfiprintf_r` and below),
  plus 152 B of nested interrupt frames (SysTick 32 + 8, EXTI 32 + 20, USART2 32 + 28) gives 1016 B against
  the 1024 B reserve: 8 B left. Measured by painting: 940 B in S3 (916 B in S1); the static value is 76 B
  (8 %) above it.
- Heap: `heap_used` is 0 B in every run (the allocator is reachable in the call graph through
  `vsnprintf` but never taken, because `say` gives it a fixed 160 B buffer).

The 1024 B is the budget the linker checks. The stack grows down from the end of RAM, so nothing is
corrupted if it is exceeded today; the room is the 8456 B that are free.

**Cause in the architecture.** One stack serves the loop and every interrupt, so its size is set by the
single deepest path in any task, here the formatted print of a console report, plus the nested interrupt
frames. Every task's worst chain draws on the same budget. The flash limit comes from the part (32 KB)
and from the choice of HAL and printf, and decides whether an OS fits at all.

**Workaround without an OS.** `-Os`; replace the printf family with hand-written number output (about
2.6 KB of flash and most of the 864 B chain; not built, estimate from the size map); use LL drivers
instead of HAL in the largest modules; avoid 64-bit division. Cost: less tested formatting code,
less readable drivers.

**What an OS would give.** A stack per task sized to that task (the console task gets the deep one,
E1 and T2 small ones), and overflow checks and high-water marks.

**What an OS would cost.** Kernel: 5..10 KB of flash and 236 B of RAM, plus 64 B per task and 76 B per queue
plus their storage (FreeRTOS FAQ, [freertos.org/FAQMem.html](https://FreeRTOS.org/FAQMem.html),
external estimate, to be measured in LR4). Against 1060 B free this does not fit without 4..9 KB of
trimming. Stack RAM becomes the sum of the task stacks instead of the maximum of the chains, with
one margin per task. As a guess for this product (console 1.0 KB for the deep chain, logger 0.4 KB, E1, T2 and idle
0.3 KB each, kernel objects 0.5 KB), the tasks add about 2..3 KB, which moves RAM from 31 % to about
40..55 % depending on how much of the 1024 B reserve is given back. The context switch time and the
tick overhead are not measured yet.

**Hypothesis for LR4.** Flash over 100 % before trimming; the first LR4 measurement is how much `-Os`
and removing printf give. RAM about 40..55 % (the guess above, to be checked with the same
`scripts/lr3_memory.sh`). `heap_used` stops being 0 if the kernel uses its own heap pool; to be reported
separately.

## Which limitation matters most

Limitation 1. It is the only one where a requirement of the product (E1, a safety-like stow request)
fails in a situation that occurs in normal operation (the logger writes a 13.5 ms block every time 16
records are ready, 80 times in S2), by 8.5 ms in the worst case measured (S3), and with no way to stay
inside the deadline without either moving the work into an interrupt or cutting every long section. The mechanism of an OS
that solves it is preemptive priority scheduling with a task notified from the interrupt. The new risks
it brings are shared tracker and servo state between the E1 and T2 tasks (mutex or queue, priority
inversion), the interrupt masking of Cortex-M0+ critical sections, and a stack to be sized for the new
task.

## What the super loop does better

These must still hold in LR4, so each has the value to compare with.

| Property | Measured value in LR3 | What to check in LR4 |
|---|---|---|
| Determinism of the periodic schedule | S1: 5000 T2 runs, 0 skipped, late max 5.86 ms, period max 54.0 ms ([stats-s1-idle.txt](metrics/stats-s1-idle.txt)); 2 h host soak: 144040 runs, 0 skipped ([soak.txt](metrics/soak.txt), real `app.c` on the host, not the ARM build) | T2 start is not later than one tick in S1 |
| Memory | heap 0 B, one stack, RAM 31.18 %, 8456 B unused | totals from the same tool; heap pool of the kernel reported |
| Start-up | first transmitted frame 4.8 ms, self-test ends 48.9 ms, first normal loop pass 54.7 ms, first T2 run 102.7 ms (edges of D4, D0 and D1 in [burst.vcd](captures/lr3/burst.vcd), read by `tools/lr3_startup.py`, output in [startup.txt](metrics/startup.txt)) | the same milestones with the scheduler started |
| Explainability and debugging | every large latency is a sum of known sections: 11.74 ms per queued reply = 8.667 + 0.75 + 2.33; E1 about 21 ms minus `d`; one thread, one stack, deterministic scenarios | whether the same latencies can still be explained once tasks preempt each other |
| No bugs between tasks | no mutex, queue, priority inversion or deadlock; the only concurrency is three interrupts with fixed priorities | the number of kernel objects used and every inversion or deadlock met |
| Low overhead | no tick work beyond SysTick, no context switch | S1 CPU load: 16 per mille now |

## Not counted in the registry

These are real but were not caused by the super-loop architecture, so they are not entries above.

- **The log is not circular.** It is full after 6533 s (108.9 min) at the S2 rate: 65536 records in
  4096 blocks, then 6748 records dropped in 2 h, 0 flush errors ([soak.txt](metrics/soak.txt)). After a
  restart it begins again at block 8192 with sequence 0 (`logger_init` only clears the state).
  It also records only changes, so a settled tracker writes nothing. These are decisions of the logger,
  fixable in the same architecture.
- **Clock scale.** The firmware time base runs 74..96 ppm fast against the simulator's time (S1:
  250024 ms for 250000 ms; [stats-s1-idle.txt](metrics/stats-s1-idle.txt)). A limit of the method.
- **No DWT and no hardware divide on the Cortex-M0+.** The cycle counter is built from SysTick and
  the tick count (88 cycles per read, 7.3 us), and wraps every 357.9 s at 12 MHz. Differences of
  unsigned values stay correct for spans shorter than that.
- **PWM frame.** A new pulse width appears at the next 20 ms frame, which bounds the end-to-end time of E1.
- **Wokwi limits.** A session is cut at 5 minutes and long runs cannot have a logic analyzer capture.

## What the simulator could not verify

| Item | What was done instead |
|---|---|
| Run of 1 h or longer, and the 358 s wrap of the counter | host tests and a 2 h virtual-time soak of the real `app.c` with fake drivers ([soak.txt](metrics/soak.txt)); the ARM build was not run that long |
| Restart of the firmware | no reset command and no watchdog; the restart behaviour of the log is a design fact (above) |
| Power loss during a flash write | not applicable: the firmware has no internal flash writes; a log record on the card carries a checksum (sum of the first 28 bytes with a seed, not a CRC) |
| Card re-inserted at run time | a scenario cannot connect a wire; the retry after a failed flush is covered by the host test `test_a_failed_write_keeps_the_records_and_waits_a_second_before_the_next_try` |
| A card or ADC that answers but never finishes | bounds computed from constants (limitation 6) |
| T3 (park) under the S3 load | argued: T2 late max in S3 is 21 ms and no run is skipped, so the 20th dark sample stays on its grid and the park command comes at most about 1.02 s after the light goes out, against 1.1 s. T3 was measured separately: parked between 0.92 s and 1.03 s, 16 of 16 cycles ([stats-park.txt](metrics/stats-park.txt)) |
| The overrun error flag of the USART | the simulator never sets it, so the handler code that clears the flags has not run in a test; only the application's reaction to a reported bus error is tested on the host |
