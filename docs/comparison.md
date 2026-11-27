# Comparison: super loop and FreeRTOS
 
Both firmwares implement the same product (FR-01..FR-08, the same command interface, the same
`STAT`/`EVT`/`CMD` line formats) and were measured with the same scenarios, the same analyzer
scripts and the same Wokwi stand on 2026-11-26. The design of the RTOS firmware is in
[rtos-design.md](rtos-design.md); the instrumentation and the scenario list are in
[baseline-lab4.md](baseline-lab4.md); the 16 hypotheses come from the registry in
[limitations.md](limitations.md). Times are microseconds (us) unless a unit is written; "pm" is per
mille of CPU time.
 
Rows marked **†** were measured on the RTOS ELF `76905721e8a1`, which has no event-task watchdog
(rtos-design.md, section 8). The final ELF `ee5ac2e18182` has it; its S2/S3 runs are tagged
`rtos-final` and, where they exist, replace the † values. The watchdog changes the CPU rows by
about +28 pm and the latency rows by noise only, except where this document says otherwise.
 
## 1. What was compared and how reliable each number is
 
| Column | Firmware | Compiler and optimisation |
|---|---|---|
| Loop | LR3 super loop at commit `04eed11` (the merged `lab3`), no later change: `firmware/build/lr3-ref/firmware.elf` | `OPT = -Os`; the LR3 numbers of `baseline-lab4.md` sections 5 and 6 were taken at `-Og` and are not mixed with these |
| FreeRTOS | `firmware/build/rtos/firmware.elf` of the `lab4` branch, FreeRTOS-Kernel V11.1.0, port `ARM_CM0` | `-Os` |
 
| Result directory (`docs/metrics/lr4/…`) | ELF | What it holds |
|---|---|---|
| `loop` | `lr3-ref` | every loop number |
| `rtos` | `76905721e8a1` (†) | `e1-idle`, `s2-nominal`, `s3-worst`, `stow`, `stow-busy` |
| `rtos` | `3abf0e9bd068` (earlier build: no kernel fix F1, a log-drain bug) | `s1-idle`, `stats`, `load`, `flood`, `burst`, `sweep-ms`, `sweep-uart`, `nosd-log`, `park`. The changed code is the task selection and the log drain, which these scenarios hardly touch; the three presses of each `sweep-ms` window were all accepted |
| `rtos-wd` | `ee5ac2e18182` (final) | `e1-idle` |
| `rtos-final` | `ee5ac2e18182` (final) | `s2-nominal`, `s3-worst`, `stats` (see section 2) |
| `rtos-t100`, `rtos-t100-unpatched`, `rtos-nowd-failed` | variants | 7.2.6, and the two runs that exposed the kernel defects |
| `captures/lr4/os/` | one ELF per scenario | the six scenarios of section 7.2 |
 
Two differences between the columns are **not** caused by the kernel: the `lab4` tree has its own
number formatter (`fmt.c`, replaces newlib `printf`) and drives TIM3 by registers instead of the
HAL. Both were needed to fit the kernel into 32 KB. The loop column was built without them, so
the memory and code-size rows of section 4 show the loop in both forms. The ELF of the loop run
is not hashed in the capture logs; the hash of the RTOS runs is known only for the builds above.
 
Compiler: all sizes in this document were produced with `arm-none-eabi-gcc` 13.2.1 and
`tools/lr4_footprint.py` on the map files. The Docker image of the project (10.3.1) produces about
100 B more (`firmware.bin` 32,244 B against 32,144 B).
 
## 2. Comparison table (LR4.pdf, section 7.3)
 
Winner: the column that is better for the product; "tie" within noise or both inside the limit
with nothing to choose.
 
| Metric | Scenario | Loop | FreeRTOS | Difference | Limit (PRD) | Winner |
|---|---|---|---|---|---|---|
| E1 stow, max | S3 (30 presses) | 17,851 | 363 † (29 of 30 presses accepted) | -17,488 (x0.02) | 10,000 | **RTOS** (loop misses) |
| E1 stow, avg | S3 | 7,217 | 326 † | -6,891 | - | RTOS |
| E1 stow, max | S2 (4 presses) | 3,757 | 458 † | -3,299 | 10,000 | RTOS |
| E1 stow, max | e1-idle (40 presses) | 5,807 | 358 † / 1,348 (final ELF) | -5,449 / -4,459 | 10,000 | RTOS |
| E1 stow, edge to the first instruction of the task | stow | not measurable (the handler runs in the main loop) | 135..148 | - | - | RTOS |
| Edge to the new servo pulse | stow-busy (press during an SD write) | 31,133 | 12,278 † | -18,855 | 30,000 | RTOS |
| T2 start late, max | S1 | 4,618 | 81 | -4,537 (x0.02) | 25,000 | RTOS |
| T2 start late, max | S2 | 11,077 | 92 † | -10,985 (x0.01) | 25,000 | RTOS |
| T2 start late, max | S3 | 20,363 | 2,237 † | -18,126 (x0.11) | 25,000 | RTOS |
| T2 start late, avg | S3 | 3,266 | 91 † | -3,175 | - | RTOS |
| T2 period, max | S3 | 66,839 | 52,153 † | -14,686 | - | RTOS |
| T2 skipped runs | S3 | 0 | 0 † | 0 | 0 | tie |
| T1 sense (wall clock), max | S3 | 726 | **3,054 †** | +2,328 (x4.2) | 5,000 | **loop** (both inside the limit) |
| T1 sense, max | e1-idle | 661 | **5,194 †** (missed) / 671 (final ELF) | +4,533 / +10 | 5,000 | loop |
| T2 body (wall clock), max | S3 | 1,084 | 3,302 † | +2,218 (x3.0) | - | loop |
| T4 command, max | S3 | 19,084 | 27,037 † | +7,953 (x1.4) | 100,000 | loop (both inside the limit) |
| T5 block write, max | S3 | 13,366 | 14,139 † | +773 | - | tie |
| T4 reply in a burst of 20 | burst | 8 of 20 within 100 ms | 9 of 20 | +1 | 20 of 20 | tie (both fail, limit of the wire) |
| Bytes lost, flood | flood, 6 windows of 1,440 | 0 / 32 / 131 / 247 / 506 / 604 | 0 in all six | -1,520 in total | 0 | **RTOS** |
| Bytes lost, flood | `stats` reply window | 224 of 486 | 224 of 486 | 0 | 0 | tie (both fail) |
| Bytes lost, S3 | S3 | 0 | 0 † | 0 | 0 | tie |
| Receive ring, max | flood, plain windows | 179..256 | 49 | -130..-207 | 256 | RTOS |
| Receive interrupt, avg | S3 | 106 cycles (8.8 us) | 207 cycles (17.3 us) † | +101 cycles | - | loop |
| CPU load | S1 | 16 pm | 23 pm (about 51 pm on the final ELF) | +7 pm (+35) | - | **loop** |
| CPU load | S2 | 34 pm | 44 pm † | +10 pm | - | loop |
| CPU load | S3 | 59 pm | 72 pm † | +13 pm | - | loop |
| Presses accepted, all runs | S3 / e1-idle | 30 of 30 / 40 of 40 | 29 of 30 † / 40 of 40 | -1 in S3 | all | **loop** |
| Stack in use | S3 | one stack, 1,108 B painted (budget 1,024 B; the heap reserve below it is unused) | main stack 464 B + task stacks 3,840 B allocated, peaks 2,532 B | - | - | see section 4 |
| Flash (same source tree) | `.text + .data` | 24,340 B (74.3 %) | 32,144 B (98.1 %) | +7,804 B (+32 %) | 32,768 B | **loop** |
| Flash (the LR3 reference `04eed11`) | | 28,508 B (87.0 %) | 32,144 B | +3,636 B (+13 %) | 32,768 B | loop |
| Static RAM incl. linker reserve | | 4,188 B (34.1 %) | 10,768 B (87.6 %) | +6,580 B | 12,288 B | **loop** |
| Heap | `heap_1` pool | 0 B | 6,144 B pool, 5,040 B used | +5,040 B | - | loop |
| Code, lines (non-blank, non-comment) | app, devices, platform, Core | 3,283 | 4,064 (+272 measurement-only) | +781 (+24 %) | - | loop |
| Start-up | first T2 run | 99.3 ms | 106.7 ms | +7.4 ms | 1 s | tie (loop by 7 %) |
| Context switch, one way | `ctx` | - | 1,041 cycles = 86.8 us (avg 90 us) | - | - | - |
| Tick interrupt | `ctx` | SysTick only | 140 cycles = 11.7 us avg, 36 us max, about 1.4 % at 1 kHz | - | - | - |
 
S1 CPU on the final ELF: 23 pm measured without the watchdog plus the +28 pm that the watchdog
added in `e1-idle` (47 -> 75 pm); the S1 run itself on the final ELF is pending.
 
Reading the table in one paragraph: the RTOS is far better exactly where LR3 predicted (the
reaction to the button and the start of the periodic task while something long runs: x0.02 and
x0.11 of the loop's values; the receive buffer never overflows while the console is busy). It is
worse where cost is counted: 7.8 KB more flash, 6.6 KB more RAM, a third more code, 7..13 pm more
CPU (35..42 pm with the watchdog), a double receive interrupt, and short tasks that are no longer
shielded from each other: T1 grew from 0.73 to 3.05 ms in S3 and `T4` from 19 to 27 ms. Nothing
that the loop met is lost, except one button press in S3 on the build without the watchdog.
 
## 3. Details behind the table
 
### 3.1 S1, S2, S3 and E1
 
| | Loop S1 | RTOS S1 | Loop S2 | RTOS S2 † | Loop S3 | RTOS S3 † |
|---|---|---|---|---|---|---|
| T2 runs / missed | 5,000 / 0 | 5,001 / 0 | - | - | 946 / 0 | 948 / 0 |
| T2 late avg / max | 2,615 / 4,618 | 81 / 81 | 3,219 / 11,077 | 84 / 92 | 3,266 / 20,363 | 91 / 2,237 |
| T2 period min..max | 46,997..54,003 | 49,998..50,000 | - | - | .. 66,839 | .. 52,153 |
| T1 sense max | 630 | 631 | 726 | 631 | 726 | 3,054 |
| T4 max | 2,373 | 2,531 | 12,052 | 14,737 | 19,084 | 27,037 |
| T5 blocks / write max | - | - | 80 / - | 80 / - | 28 / 13,366 | 28 / 14,139 |
| E1 n / avg / max | - | - | 4 / 1,101 / 3,757 | 4 / 424 / 458 | 30 / 7,217 / 17,851 | 29 / 326 / 363 |
| CPU pm | 16 | 23 | 34 | 44 | 59 | 72 |
| Rx dropped | 0 | 0 | 0 | 0 | 0 | 0 |
 
The periodic task is exactly periodic on the RTOS (S1 period 49,998..50,000 us) because it
starts from a tick, whereas the loop starts it at the next pass, up to one pass (about 6 ms in the
idle loop; an SD write or a reply in progress adds up to 20 ms). In S3 the RTOS T2 late maximum
is 2.2 ms, not 0.1 ms. The same S3 shows T1 at 3.05 ms and the T2 body at 3.3 ms wall clock: all
three are explained by one event, a press arriving just before a T1/T2 release, because the event
task prints the `EVT stow` line itself (section 5).
 
E1 by press kind (S3 loop): `sd` presses, 27 of them, avg 7,599 us, max 17,851 us, nine above 10 ms,
latency falling with the time the press arrives after the `sd-write` line (3 ms: 17.2..17.9 ms,
15 ms: 2.9..5.1 ms); `random`, 3 presses, max 5,089 us. The RTOS has no such dependence:
267..363 us for all 29 accepted presses (median 325 us).
 
E1 in `e1-idle` (idle system, 40 presses): loop min 157, median 2,357, max 5,807 us, uniformly
spread over the 6 ms pass; RTOS † min 269, median 319, max 358 us (all 40 below 1 ms); final ELF
with the 8 ms watchdog min 269, median 320, p99 and max 1,348 us (39 below 1 ms). The one press
at 1,348 us is consistent with a wake-up that was recovered by the watchdog poll (not proven);
the loop's maximum was 4.3x larger.
 
### 3.2 Console and receive buffer
 
* **Flood** (1,440 bytes at wire speed, one window per amount of work `X` between the bytes):
  loop lost 0, 32, 131, 247, 506 and 604 bytes for X = 0, 8, 17, 34, 68 and 100 us (ring full at
  256 B every time except X = 0); RTOS lost 0 in all six, ring peak 49 B.
* **`sd-write` window** (255 bytes sent while the console writes an SD block): 0 lost, ring peak
  240 B, in both firmwares.
* **`stats` reply window** (486 bytes sent while the reply is printed): 224 lost in **both**
  firmwares (262 taken = ring 256 B + 6 read before). The numbers are identical because the
  consumer of the ring is the task that is printing: while it prints the 1 kB reply with the
  polled UART nobody takes bytes from the ring. Scheduling cannot change this; more buffer, a
  receive task that does not print, or a printing path that does not block the consumer would.
* **Burst of 20 `track` lines** (100-byte replies): loop answers 8 within 100 ms (one reply per
  11.95 ms), RTOS 9 (one per 11.07 ms: 9.97 ms of wire time for 100 B plus 1.15 ms between the
  replies). On the RTOS the limit is the wire; the loop spent an extra 2.3 ms per reply, the RTOS
  1.15 ms. T4 for lines 10..20 is still missed (11 of 20). Both T2 series in the burst: loop
  60.0, 47.9, 48.0, 47.9 ms; RTOS 50.0 ms four times.
* **Receive interrupt**: 207 cycles average on the RTOS against 106 in the loop; the difference is
  the `xSemaphoreGiveFromISR` and the context switch request. Maximum 567 against 114 cycles
  (47 us against 9.5 us), still far below a byte time of 86.7 us.
### 3.3 Load sweeps
 
`sweep-ms` (a synthetic pass of X ms in the lowest-priority task / in the loop, 3 presses per
window):
 
| X, ms | Loop: T2 late max, E1 max | RTOS: T2 late max, E1 max |
|---|---|---|
| 1 | 11,001, 5,552 | 116, 344 |
| 2 | 12,157, 5,565 | 97, 329 |
| 4 | 10,964, 9,406 | 81, 325 |
| 6 | 11,775, **12,059** (E1 missed first) | 97, 340 |
| 10 | 15,285, 12,897 | 81, 366 |
| 20 | 27,794 (**T2 missed first**), 19,894 | 81, 358 |
| 30 | 33,991, 23,570 | 81, 358 |
 
The RTOS has no break point up to 30 ms (E1 at most 378 us by the pin, T2 at most 116 us late);
the loop breaks E1 at 6 ms and T2 at 20 ms, as in LR3.
 
`sweep-uart` (bytes arriving while the work runs): no byte lost by either firmware. The RTOS
column shows the ring at 48 B, the loop's at 1..22 B (the loop empties the ring more often but
with more jitter).
 
### 3.4 Failing device
 
`nosd-log` (SD card unplugged, logging on): both firmwares end with `dropped=144 errors=12`. Loop:
T2 late max 4,613 us, T4 max 10.2 ms; RTOS: T2 late max **81 us**, T4 max 9.9 ms. The failing
card initialisation (about 4 ms once per second) blocks the loop; on the RTOS only the logger
task waits.
 
### 3.5 Parking (T3)
 
`park` (light switched off and on 16 times): loop and RTOS both give 16 of 16 right at each of
the three probes, 0 missed T2 runs; panel not parked at 920 ms, parked at 1,030 ms after the
light went out (deadline 1.1 s). A dark counter of 18 appears 13 times on the loop and 8 times on
the RTOS (the phase of the 50 ms grid), which does not change the result.
 
## 4. Memory, code size, start-up
 
| | Loop (`04eed11`, `-Os`) | Loop (lab4 tree) | FreeRTOS (final) |
|---|---|---|---|
| Flash `.text + .data` | 28,508 B (87.0 %) | 24,340 B (74.3 %) | 32,144 B (98.1 %), margin 624 B (524 B with the Docker compiler) |
| Static RAM + reserve | 4,132 B (33.6 %) | 4,188 B (34.1 %) | 10,768 B (87.6 %) |
| of which heap pool | 0 | 0 | 6,152 B array, 5,040 B used, 1,104 B free |
| Stacks | one, 1,108 B painted | one, 1,108 B | main 464 B + tasks 3,840 B allocated (peaks 2,532 B, 66 %) |
 
Flash by group (bytes; `tools/lr4_footprint.py`, map files):
 
| Group | Loop `04eed11` | Loop (lab4 tree) | FreeRTOS |
|---|---|---|---|
| Core, startup | 1,922 | 1,814 | 1,814 |
| HAL | 8,540 | 6,968 | 6,968 |
| platform | 1,892 | 1,976 | 2,028 |
| devices | 2,722 | 3,182 | 3,182 |
| app | 4,975 | 5,575 | 5,529 |
| RTOS layer (`rtos_app.o`) | - | - | 2,086 |
| kernel (`tasks_lr4` 2,684, `queue` 1,684, `port` 512, `portasm` 166, `list` 104, `heap_1` 146) | - | - | 5,296 |
| libc | 4,340 | 716 | 716 |
| libgcc | 956 | 956 | 956 |
| string pool | 3,161 | 3,153 | 3,569 |
| **Total** | **28,508** | **24,340** | **32,144** |
 
Kernel RAM is 6,400 B, of which the heap array 6,152 B and the kernel's own data 248 B; the RTOS
layer holds 184 B. The kernel plus layer cost 7,382 B of flash. On the unmodified LR3 firmware that
would have been 28,508 + 7,382 = 35,890 B = 109.5 % of the flash: the kernel fits only because the
lab4 tree first dropped newlib `printf` (-3.6 KB of libc) and the HAL TIM module (-1.6 KB of HAL).
The kernel figures from the FreeRTOS FAQ (5..10 KB of flash, about 236 B of RAM) are met for
flash (5.3 KB) and for the kernel's own data (248 B); the rest of the RAM is the pool: task stacks
3,840 B, six task control blocks about 430 B, queue 328 B, mutexes and semaphores about 430 B.
 
Code: `app` 1,031 -> 1,182, `devices` 924 -> 1,058 (`fmt.c`, 128 lines), `platform` 612 -> 648,
`Core` 716 -> 709, plus the RTOS layer (`firmware/rtos/src` 402 lines, `inc` 65 lines, and 272 lines
of scenario and diagnostic code under `#ifdef`) = 3,283 -> 4,064 lines. Of the +781 lines 467 are
the RTOS layer and 151 are hooks and service entry points in `app.c`; the application layer stays
free of kernel headers (`scripts/check_deps.sh`), and the 703 host checks of `test_app.c` run on
both firmwares.
 
Start-up (logic analyzer, `stats` capture, t = 0 at reset): first byte on the UART 3.4 ms (loop)
and 5.0 ms (RTOS); first T2 run 99.3 ms (loop) and 106.7 ms (RTOS), +7.4 ms. Of the
difference 1.7 ms is already there at the first UART byte (cause not examined) and about 5.4 ms is
the transmission of the line `RTOS up tick_hz=1000 core_hz=12000000 heap_free=1432 tasks=5`
(62 bytes with CR LF at 86.7 us); the remaining 0.3 ms covers task creation and the scheduler
start. Limit T6: 1 s, met by both.
 
## 5. Chronology of one press (preemption)
 
![Chronology](img/timeline-stow-busy.png)
 
Scenario `stow-busy`: a command that writes an SD block is running when the button is pressed
(analyzer: D3 button, D2 `EVENT` pulse of the stow handler, D0 busy pin PB6, D6 SD chip select,
D4 UART TX, D5 servo 1 PWM; `tools/lr4_timeline.py` draws the figure from `stow-busy.vcd`).
 
| Moment (after the first falling edge) | Loop | FreeRTOS † |
|---|---|---|
| Handler (`EVENT`) starts | 18,190 us | **135 us** |
| Handler ends | 18,278 us | 309 us |
| New servo pulse starts | 31,133 us | 12,278 us |
| The command that was running | ends first, then the press is served | preempted, resumes after the press |
| Busy stretch that contained the edge | 23,313 us | 23,815 us |
 
On the loop the press is noticed only when the pass that holds the SD write and its reply ends
(reaction = rest of the pass; the edge fell 706 us into a 23 ms busy stretch). On the RTOS the
button interrupt makes the `event` task (priority 5) ready, it runs 135 us after the edge and
preempts `cmd`; the chip select of the command's SD write goes low at 5.3 ms after the edge
instead of 0.5 ms in the loop, because the event task and its print run first. The sum of the
work is the same (the busy stretch is 23.3 and 23.8 ms), the order differs.
 
The figure also shows what the RTOS costs. Right after the 0.17 ms handler the `event` task
prints `EVT stow ...` with the polled UART: **UART TX from 1.0 to 4.2 ms**, 3.2 ms at priority 5.
For that time no lower task runs: `track` (T1, T2) and `cmd` are held. This is the probable
cause (it is read from this record; no run was done with the print moved) of T1 up to 3.05 ms in
S3 and of T1 5.19 ms (e1-idle †), 5.34 and 5.45 ms (two S3 runs at 100 Hz), which are above the
5 ms limit, whenever a press falls into the 0.6 ms of a T1 run. The hypothesis registry did not
foresee that the cost of a long print moves from the loop to the other tasks. The fix is to give
the print to `cmd` or `logger` through the queue; it was not made because it changes the measured
firmware (open question in rtos-design.md).
 
## 6. Scenarios of LR4.pdf, section 7.2
 
| 7.2 | Scenario (`scenarios/os-*.yaml`) | Result | Reading |
|---|---|---|---|
| 1 | priority inversion: low (prio 1) holds a lock, high waits, a medium task (prio 2, 60 ms) runs | mutex (priority inheritance): high task waits **21.5 and 20.3 ms**; binary semaphore: **83.0 and 82.7 ms** | with a mutex the wait is the low task's 20 ms; with a binary semaphore the medium task's 60 ms is added, and in general is unbounded. The shared resources of the product therefore use mutexes |
| 2 | stack overflow of the `load` task (576 B) with the check on and off | check off: no message; the neighbouring `logger` control block was overwritten (`run_us=202115052`), the window ran 356 ms instead of 300; check on: `FATAL stack overflow in load`, firmware halted. A first build stayed **silent** | detection is at the next context switch, after the damage: a last resort, not protection. The silent build exposed that the hook printed through the HAL, which answers HAL_BUSY while another task is transmitting; it now writes the UART register directly (`plat_uart_panic_write`) |
| 3 | starvation: `hog` task at priority 5 polls the clock for 300 ms | `track` late max **286.6 ms**, 5 runs missed, `cmd` reply only after the window; the `event` task (same priority) still ran by time slicing: `EVT stow` latency 1,004 us (idle 150..350 us); `hog` took 237 pm of CPU | an endless task at the top of the table stops everything below it; equal priority shares time in ticks, so E1 degrades to one tick |
| 4 | queue exhaustion: `qflood 64 0` and `qflood 64 100` on the 16-record log queue | wait 0: **16 sent, 48 lost** in 2.9 ms; wait 100 ms: 64 sent, 0 lost, but the `cmd` task blocked for 61.8 ms and the `track` task (higher priority) lost 1 record | a blocking sender trades lost data for latency; a full queue is the only place where a higher-priority producer still loses data |
| 5 | heap exhaustion: pool 4,000 B instead of 6,144 B | hook build: `FATAL malloc failed`; no-hook build: `FATAL assert line=569` (the line of the assert in `add_task`): no cause, no task name | both halt before the scheduler starts; the hook costs flash but names the cause |
| 6 | tick 1,000 -> 100 Hz, S3 (`os-t100`, no watchdog) | E1 30 of 30, avg 313, max 348 us; T2 late max 1,953 us (1 kHz 2,237); T1 max **5,454 us** (missed, 1 kHz: 3,054); T4 27.0 ms; CPU 72 pm (72 at 1 kHz); T5 15.8 ms; dropped 0 | the tick costs 1.4 % of the CPU at 1 kHz and 0.14 % at 100 Hz (computed from the 11.7 us tick body; it runs in idle time and does not appear in the per-mille load); E1 and T2 do not need the fine tick (E1 is interrupt-driven, T2 is 5 ticks); the two T1 outliers above 5 ms have the cause of section 5 |
 
Other failures that the runs exposed are in the problem log of the report: the stale
`uxTopReadyPriority` (4 of 40 presses accepted; an unpatched kernel at 100 Hz accepted 14 of 30,
all before 21 s, the same signature) and the lost wake-up with a token present (8 of 40 accepted
by the final ELF without watchdog, a diagnostic build showed the task blocked with the semaphore
holding a token and 2,697 edges pending).
 
## 7. Hypotheses of the registry (LR4.pdf, section 7.4)
 
Verdicts: **confirmed**, **partly**, **not confirmed**. "Baseline" is the loop re-measured on
2026-11-26 where it was re-measured, else the LR3 value.
 
| Id | Scenario and metric | Baseline | Hypothesis | LR4 result | Verdict |
|---|---|---|---|---|---|
| H1a | S3, E1 max over 30 presses | 17,851 us (LR3: 18,511) | at most 1 ms | 363 us † (29 of 30 presses; min 267) | **confirmed** for the latency; one press was lost, see H5 |
| H1b | e1-idle, E1 max | 5,807 us (LR3: 6,113) | at most 1 ms | 358 us † (40 of 40); 1,348 us on the final ELF (39 of 40 below 0.35 ms) | **partly**: confirmed without the watchdog, one press of 40 at 1.35 ms with it |
| H1c | stow-busy, edge to the new servo pulse | 31,133 us (LR3: 31,370) | at most about 21 ms | 12,278 us † | **confirmed** |
| H2a | S1, S2, S3, T2 start late max | 4,618 / 11,077 / 20,363 us | at most 2 ms in all three | 81 / 92 † / 2,237 † us | **partly**: S1 and S2 yes; S3 2.24 ms, above 2 ms by 0.24 ms (cause: section 5, probable) |
| H2b | window with a `stats` reply: skipped T2 runs, late max | 1 run, 54,989 us | 0 runs, at most 2 ms | 0 runs, 97 us | **confirmed** |
| H3a | burst of 20 `track` lines: replies within 100 ms | 8 | about 10, the limit of the wire | 9 (one reply per 11.07 ms, which is the wire time plus the gap) | **confirmed** (the limit of the wire is 9) |
| H3b | `time` window of e1-idle: skipped T2 runs | 17 (LR3: 18) | 0 | 0 (22 T2 runs in the window against 3) | **confirmed** |
| H4a | flood, six windows: bytes lost of 1,440 | 0 / 32 / 131 / 247 / 506 / 604 (LR3: 53..657) | 0 in all six | 0 in all six | **confirmed** |
| H4b | flood, `sd-write` window | 0 of 255, ring max 240 | 0 lost | 0 of 255, ring max 240 | **confirmed** (also 0 on the loop) |
| H4c | flood, `stats-reply` window | 224 of 486 | at most about 30 | **224 of 486** | **not confirmed**: the consumer of the ring is the printing task, section 3.2 |
| H5 | at least 100 presses: accepted equals sent, no backward time stamp | e1-idle 40 of 40, S3 30 of 30 | 0 anomalies; time from the EXTI handler to the first instruction of the E1 task on PB7 | e1-idle 40 of 40 (both ELFs), **S3 29 of 30 †**, no backward time stamp; edge to task 135..148 us. Before the kernel fix: 4 of 40 and 8 of 40; unpatched kernel at 100 Hz 14 of 30 | **not confirmed**: presses were lost in four runs; two kernel-level mechanisms are known (rtos-design.md, section 8); the S3 loss on the ELF without watchdog is probably the second one, not proven |
| H6 | nosd-log: T2 late max and E1; `dropped` | 4,613 us, `dropped=144` | unaffected, at most 2 ms; `dropped` 144 | 81 us, `dropped=144` | **confirmed** |
| H7a | sweep-ms: first X where E1 or T2 breaks | E1 at 6 ms, T2 at 20 ms | no break up to 30 ms | none up to 30 ms (E1 at most 378 us, T2 late at most 116 us) | **confirmed** |
| H7b | places to edit for one new periodic task | at least 4 | 2 (the task function and its creation) | 2: the task function and one `add_task(...)` line (the table of handles for `tasks`, the stack and priority report are filled inside `add_task`); plus a priority and a stack-size constant. Memory is the limit: 1,104 B of the pool is free | **confirmed** |
| H8a | flash before trimming | 96.77 % (LR3, `-Og`); 87.0 % at `-Os` | over 100 % | 109.5 % computed for the loop plus kernel and layer (28,508 + 7,382 B); the shipped build is 98.1 % after the formatter and TIM changes | **confirmed** (by arithmetic: the untrimmed build was never made) |
| H8b | RAM, same tool | 31.18 % (LR3, `-Og`); 33.6 % at `-Os` | about 40..55 %; `heap_used` separate | **87.6 %** (10,768 B), of which the pool 6,144 B (5,040 B used); outside the pool 37.6 % | **not confirmed**: 40 % is the heap alone, and it was added to the static data |
 
Count: 11 confirmed (H8a by arithmetic only), 2 partly (H1b, H2a), 3 not confirmed (H4c, H5,
H8b). H1a, H2a and H5 depend on the S3 re-run on the final ELF (section 1).
 
New problems that the registry did not list, all measured above:
 
1. **Long output moves to the high-priority task** (section 5): T1 up to 3.05 ms in S3, three runs
   above 5 ms.
2. **Lost wake-ups of the event task** (two kernel-level mechanisms, section 8 of rtos-design.md),
   solved by a kernel change and an 8 ms watchdog at +28 pm of CPU.
3. **RAM is the new limiting resource**: 87.6 % of the RAM and 98.1 % of the flash are used; the
   next feature does not fit without trimming the stacks (the peaks use 66 % of the allocation).
4. **The consumer of the receive ring is still a single task** (H4c).
## 8. What the super loop does better (LR3 list, re-checked)
 
| Property | Value in LR3 | Value in LR4 | Verdict |
|---|---|---|---|
| Determinism of the schedule | S1: 5,000 runs, 0 skipped, T2 late max 4.6 ms, period max 54.0 ms | RTOS S1: 5,001 runs, 0 skipped, late max 81 us, period 49,998..50,000 us | **RTOS better on measured jitter.** What remains with the loop: every path is the same code on every run; the RTOS has a defect that depends on the code layout (an earlier ELF passed 40 of 40, the next accepted 8 of 40) |
| Memory | heap 0 B, one stack, RAM 31..34 % | RAM 87.6 %, heap pool 6,144 B | **loop** |
| Start-up | first T2 run 99.3 ms | 106.7 ms | **loop**, by 7.4 ms; both far below 1 s |
| Explainability | every latency is a sum of known sections (11.74 ms per queued reply = 8.667 + 0.75 + 2.33; E1 about 21 ms minus `d`) | E1 135..350 us, explained by the priorities; T1 outliers above 5 ms were found only with the analyzer (section 5), the lost wake-up is not explained | **loop**, but the RTOS cost can be measured with the same tools |
| No bugs between tasks | no mutex, queue, inversion, deadlock | five tasks, three mutexes, three semaphores, one queue: two kernel-level lost-press mechanisms, one silent fault hook, log drops by starvation, a 21 ms against 83 ms inversion if the wrong primitive is used | **loop** |
| Low overhead | no tick work beyond SysTick; S1 CPU 16 pm | S1 CPU 23 pm (about 51 pm with the watchdog), context switch 86.8 us, tick 1.4 % of the CPU, receive interrupt twice as long | **loop** |
 
The rows where the loop wins are the rows that cost something the product has to pay every second
(RAM, CPU, code to maintain); the rows where the RTOS wins are the deadlines of the PRD (E1, T2,
the console that does not lose bytes). The product has one deadline that the loop did not meet
(E1: 17.9 ms in S3 against 10 ms). The RTOS meets it with a factor of 28, and misses only the burst
of 20 lines (the limit of the wire, in both firmwares) and, in three runs, T1 by 0.2 to 0.45 ms
(section 5). For this PRD the RTOS is the right architecture, provided the `EVT` print is moved
out of the top-priority task. It would not be the choice for a firmware whose only timing
requirement were the 50 ms period: there the loop passes with 24.3 KB of flash, 34 % of the RAM
and no kernel to debug.
 
## 9. Caveats
 
* The measurements are from a simulator. Times of the simulated peripherals (SPI at 6 MHz, UART
  at 86.7 us per byte, the SD card model) are not those of the silicon; the stale
  `uxTopReadyPriority` and the lost wake-up may be artefacts of the simulator (on a real core PendSV
  runs with interrupts masked). Both were handled in the firmware because the results could not
  be taken without it; neither is proven to occur on hardware.
* The RTOS rows of `s2-nominal`, `s3-worst`, `stow`, `stow-busy` and E1 come from `76905721e8a1`,
  those of the other scenarios from `3abf0e9bd068`; only `e1-idle` was run on the final ELF.
  CPU rows are about 28 pm too low for the final firmware.
* The wall-clock "execution time" rows (T1, T2 body, T4, T5) contain preemption (LR4.pdf,
  section 6.1): an increase there is the price of the preemption, not slower code.
* The loop column was built without the formatter and the register-driven TIM3 of the lab4 tree;
  the formatter and the TIM change smaller code, so the "loop (lab4 tree)" column of section 4 is
  the fairer size comparison. The timing of the loop was not re-measured on the lab4 tree.
* One S3 press of 30 was lost on the RTOS (†). The ELF with the watchdog has not shown this in
  `e1-idle` (40 of 40); its S3 run decides H1a and H5.
* The compiler of the measurements here is 13.2.1; the project's Docker image has 10.3.1 and gives
  about 100 B more flash.
 