# Analysis of the logic-analyzer captures

Captures, channel plan and parameters: [README.md](README.md). Every number below comes from the six
VCD files and can be reproduced with `scripts/decode_captures.sh` (the full output is in the
`NAME.decoded.txt` files). Times are simulation time since reset, read from 25 ns timestamps, so a
single edge is known to +/-25 ns.

Labels used in this document:

- **measured**: read from the capture;
- **calculated**: derived from the clock tree (12 MHz SYSCLK) or from a protocol definition;
- **hypothesis**: a possible explanation that has not been verified;
- **to confirm**: a number or section that has to be checked in the primary document (PDF) before it goes into the report.

## 1 Findings in short

1. All four interfaces show what the firmware reports on the UART. The decoded SD sequence
   (CMD0, CMD8, CMD55, ACMD41, CMD58, CMD17) is what the SD specification describes, the block it
   returns has a correct CRC16 (B062) and ends in 55 AA.
2. UART, PWM and the SCK frequencies match the calculated values to better than 1 %. UART 115348
   baud against 115385 calculated (BRR 104); PWM period exactly 20000 us; SCK 374.8 kHz and 5957 kHz
   against 375 kHz and 6 MHz.
3. The SPI bus is idle most of the time. The block read moves 512 bytes in 10.49 ms, which is 6.5 % of
   what the 6 MHz clock could carry. The limit is software (about 17 us per byte and about 100 us per
   call of the SPI transfer function), not the clock.
4. A new servo width reaches the pin at the next 20 ms period boundary: 17.0, 11.3 and 5.5 ms after
   the reply for the three commands in the `servo` capture. No capture shows a runt pulse.
5. The deliberate anomaly (`nosd`, no card) shows five CMD0 frames and a flat-low MISO. The driver ends
   with status 3 (BUS). The same bytes, replayed into the driver on the host, give the same status
   (`tests/test_sd_replay.c`).
6. Two things were found that nobody asked for: PB9 (CS) reads low for the first 3.85 ms after reset
   and the card model drives MISO low for 3.03 ms of that time and again for the first byte of the first
   transaction (section 8.2). The cause is the CubeMX-generated GPIO initialisation, which sets the output level
   of PB9 low.
7. Several timing results look like properties of the simulator and not of the chip (section 9). They
   have to be re-measured on hardware before they are used for design decisions.

## 2 Method

- `scripts/vcd_tool.py report` decodes the VCD itself (standard library only): UART 8N1 on D4, SPI mode 0
  on D0 to D3, SD commands on top, PWM widths on D5 and D6, marker pulses on D7.
- `scripts/vcd_tool.py crosscheck` runs sigrok-cli (libsigrokdecode `uart` and `spi` decoders) on the same
  file and compares the byte streams. On all six captures they are identical (246 to 372 UART bytes and
  35 to 1102 SPI bytes per capture).
- Rule for the SPI sampling instant: Wokwi changes MOSI at the same timestamp as the rising SCK edge
  (section 4.3). A byte is therefore read from the value that is valid after all changes with the
  timestamp of the edge. sigrok uses the same rule.
- Reading a capture: UART frames start with the falling edge of the start bit. SPI bytes are the 8 rising
  SCK edges inside one CS-low window. "Window" means one stretch of CS low.

## 3 UART (D4)

### 3.1 Decoded text (boot.vcd)

246 bytes, 0 framing errors. Start of the first frame to the end of the line feed:

| Time (ms) | Text |
|-----------|------|
| 1.8957 .. 3.6754 | `Heliostat boot OK` |
| 4.2806 .. 6.7204 | `SELFTEST BEGIN version=1` |
| 8.0950 .. 11.9486 | `SELFTEST adc OK raw=1025,2050,3074,4095` |
| 11.9776 .. 13.5694 | `SELFTEST spi OK` |
| 30.6496 .. 33.9398 | `SELFTEST sd OK type=V2HC sig=55AA` |
| 34.3627 .. 37.5565 | `SELFTEST servo1 OK pulse_us=1500` |
| 37.9827 .. 41.1762 | `SELFTEST servo2 OK pulse_us=1500` |
| 41.6606 .. 45.4202 | `SELFTEST END total=5 passed=5 failed=0` |

The text is the report format of `docs/selftest-format.md`: the decoded line is what
`scripts/check_selftest.sh` parses. The other captures add console replies after the self-test
(`CMD help OK commands=help,selftest,adc,servo,sd`, `CMD foo FAIL reason=unknown`,
`CMD ? FAIL reason=too-long`, `CMD adc OK raw=1025,2050,3074,4095`, `CMD servo OK ch=1 angle=0 pulse_us=1000`,
`CMD servo OK ch=1 angle=180 pulse_us=2000`, `CMD servo OK ch=2 angle=45 pulse_us=1250`,
`CMD sd-read OK block=0 sig=55AA first16=00000000000000000000000000000000`).

Datasheet side: USART2 on PA2 (TX) and PA3 (RX) is the ST-LINK virtual COM port of the NUCLEO board
(UM2953, solder bridges SB27 and SB32; **to confirm** the bridge names and the table number in the PDF). The decoded line is at PA2 and at 115200 baud, so the capture agrees with that choice.

### 3.2 Manual decode of the first character (no decoder)

The first frame of boot.vcd, read from the edges of D4 only. The bit time is T = 8.6694 us (fitted over
all edges of the capture). The line is low until 1.8437 ms and then idles high (sections 8.2
and 9). The start bit begins at the falling edge at 1.895700 ms.

| Edge on D4 | Time (ms) | Since start (us) | In bit times |
|------------|-----------|------------------|--------------|
| falling (start bit) | 1.895700 | 0 | 0 |
| rising | 1.930375 | 34.675 | 4.00 |
| falling | 1.939050 | 43.350 | 5.00 |
| rising | 1.956375 | 60.675 | 7.00 |
| falling | 1.965050 | 69.350 | 8.00 |
| rising | 1.973725 | 78.025 | 9.00 |

Slots of one bit time, in order of transmission (least significant bit first):

| Slot | 0 start | 1 d0 | 2 d1 | 3 d2 | 4 d3 | 5 d4 | 6 d5 | 7 d6 | 8 d7 | 9 stop |
|------|---------|------|------|------|------|------|------|------|------|--------|
| Level | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 1 | 0 | 1 |

Reading d7..d0 gives 0100 1000 = 0x48 = `H`. The next frame starts at 1.989625 ms and decodes to 0x65 `e`,
then 0x6C `l`: the beginning of "Heliostat". sigrok-cli reports the same first bytes 48 65 6C 69.

### 3.3 Timing against the calculation

| Quantity | Calculated | Measured |
|----------|-----------|----------|
| Baud rate | 12 MHz / BRR 104 = 115385 Bd (+0.16 % against 115200) | 115348 Bd (fit of all edges, +0.13 % against 115200) |
| Bit time | 8.667 us | 8.669 us |
| Frame (10 bits) | 86.67 us | 86.69 us |
| Start to start of consecutive characters | 86.67 us (back to back) | 93.925 us (median), i.e. 7.23 us idle between characters |
| Throughput | 11538 characters/s | 10647 characters/s (92.3 %) |

The baud rate agrees with the calculation to 0.03 %, which is below what a single 25 ns edge can resolve
(0.3 % of a bit). The 7.23 us between characters is software time between two transmit steps; whether the
HAL loop or the simulator's USART model causes it is **not known** (section 8.3).

## 4 SPI and the SD card (D0 to D3)

### 4.1 What the specification describes and what is on the bus

Specification: SD Physical Layer Simplified Specification, SPI mode (**to confirm** section and table
numbers in the PDF; they were not read for this document). Wokwi simulates a version 2 SDHC card.

| Step in the specification | On the bus (boot.vcd) | Match |
|---------------------------|-----------------------|-------|
| at least 74 clocks with CS high, DI high, clock at most 400 kHz | 10 bytes of 0xFF (80 clocks) with CS high, from 13.5694 ms, at 375 kHz | yes |
| CMD0 with CRC 0x95, answer R1 = 0x01 (idle) | `40 00 00 00 00 95`, R1 = 01 | yes |
| CMD8, argument 0x000001AA, CRC 0x87, answer R7 echoing the argument | `48 00 00 01 AA 87`, R1 = 01, R7 = 000001AA | yes |
| CMD55 then ACMD41 with the HCS bit, repeated until R1 = 0 | `77 00 00 00 00 01` R1 = 01; `69 40 00 00 00 01` R1 = 00 at the first try | yes |
| CMD58, OCR bit 31 (power-up done), bit 30 (CCS, block addressing) | `7A 00 00 00 00 01` R1 = 00, OCR = C0FF8000: bits 31 and 30 set | yes |
| clock raised to the normal rate after initialisation | 6 MHz from the CMD17 window on (the first window is at 375 kHz) | yes, 6 MHz is below the 25 MHz limit of the default speed mode (**to confirm**) |
| CMD17, R1 = 0, data token 0xFE, 512 bytes, CRC16 | `51 00 00 00 00 01`, R1 = 00, token FE at 19.1667 ms, 512 bytes, CRC16 B062 | yes, computed CRC16/XMODEM over the 512 bytes is B062 |
| 8 extra clocks after CS goes high | one 0xFF byte about 32 us after each CS rise | yes |

CMD0 and CMD8 are the only commands whose CRC the card checks in SPI mode (**to confirm**); the other commands carry
the dummy CRC 0x01 in the capture, as expected. The first sector is an MBR: partition 1 has type 04 (FAT16),
first LBA 63 and 16321 sectors (8.36 MB), and the last two bytes are 55 AA.

The SPI row of the minimum list in the lab text asks for "a write operation with a preceding write-enable
command". This product has no flash and the SD driver has no write operation, so there is no write-enable. The
closest thing in the capture is the prefix command pair CMD55 + ACMD41 (ACMD41 is only valid right after
CMD55, as the write command is only valid after write-enable). A block write (CMD24) is not part of these
captures.

### 4.2 Decoded transactions (boot.vcd)

Window 1 is one CS-low stretch of 3.390 ms (14.752625 to 18.142375 ms) at 375 kHz:

| Start (ms) | Command | Frame | Answer |
|------------|---------|-------|--------|
| 14.7971 | CMD0 GO_IDLE_STATE | `40 00 00 00 00 95` | R1 = 01 after 1 idle byte |
| 15.3734 | CMD8 SEND_IF_COND | `48 00 00 01 AA 87` | R1 = 01, R7 = 000001AA |
| 16.2005 | CMD55 APP_CMD | `77 00 00 00 00 01` | R1 = 01 |
| 16.7772 | ACMD41 SD_SEND_OP_COND | `69 40 00 00 00 01` | R1 = 00 |
| 17.3545 | CMD58 READ_OCR | `7A 00 00 00 00 01` | R1 = 00, OCR = C0FF8000 |

Window 2 is 11.659 ms (18.305975 to 29.965025 ms) at 6 MHz and holds CMD17 for block 0: command at 18.3495 ms,
R1 = 00 after 1 idle byte, token 0xFE at 19.1667 ms after 4 idle bytes, 512 data bytes from 19.2725 to
29.7647 ms, then the CRC. sdread.vcd contains the same transaction a second time (window 3,
51.3124 to 62.9645 ms), started by the console command.

### 4.3 Manual decode and the capture phase (no decoder)

First byte of the first CMD0 (boot.vcd). CS falls at 14.752625 ms. SCK is low before the first clock, so
the idle level is 0 (CPOL = 0).

| Rising edge | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|-------------|---|---|---|---|---|---|---|---|
| Time (ms) | 14.797125 | 14.799800 | 14.802475 | 14.805150 | 14.807800 | 14.810475 | 14.813150 | 14.815800 |
| MOSI | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 |

MOSI changes at 14.797125 (to 0), 14.799800 (to 1) and 14.802475 (to 0), which are the same instants as
the rising edges 1, 2 and 3. Reading the value after the change gives 0100 0000 = 0x40: start bit 0,
transmission bit 1, command index 000000, which is CMD0. Bytes 1 to 4 are 00 (argument 0). Byte 5 is
0x95 = 1001 0101: the CRC7 1001010 (0x4A, the same value sigrok's SD decoder prints) and the end bit 1.
Had the bit been read just before the edge, the six bytes would read `A0 00 00 00 00 4A`, which is no command.

The answer is read in the byte that starts at 15.238575 ms. MISO is already 0 before its first clock:
it fell at 15.136000 ms, at the last falling SCK edge of the previous byte, 102.6 us before this byte
starts. It rises at 15.255925 ms, which is a falling SCK edge between the rising edges 7 (15.254600) and
8 (15.257250). The levels at the eight rising edges are
0 0 0 0 0 0 0 1 = 0x01: R1 with the idle bit set, as the specification says for CMD0.

Capture phase: the card changes MISO on falling SCK edges and keeps it stable across the rising edge, and
the first bit of a byte is already on the line before the first clock edge. The master has to capture on
the rising edge, which is the first edge of the cycle: CPHA = 0. With CPOL = 0 this is SPI mode 0, the mode
the firmware configures. If the data were captured at the falling edges the first eight MISO bytes would read
`01 FF FF FF FF FF FE 03` instead of `00 FF FF FF FF FF FF 01`, which is not a valid frame sequence.

MOSI timing is a simulator property: a real SPI master in mode 0 changes MOSI on the falling edge, so
that it is stable at the rising edge. Wokwi puts the change on the rising edge (zero setup time, see 9).

### 4.4 Timing against the calculation

| Quantity | Calculated | Measured |
|----------|-----------|----------|
| SCK, initialisation | 12 MHz / 32 = 375 kHz, 2666.7 ns | 2667.9 ns = 374.8 kHz |
| SCK, normal operation | 12 MHz / 2 = 6 MHz, 166.7 ns | 167.9 ns = 5957 kHz |
| Seven SCK periods (first to last rising edge of a byte), 375 kHz | 18.667 us | 18.675 us |
| The same at 6 MHz | 1.1667 us | 1.175 us |
| 80 clocks of the power-up sequence | 213 us | 1106.5 us from first to last clock (10 separate bytes, 120.6 us apart) |
| Window 1 (5 commands, 48 bytes) | 48 x 21.33 us = 1.02 ms | 3.390 ms |
| 512-byte payload at 6 MHz | 682.7 us | 10.492 ms (first to last data byte) |
| Payload rate | 750 kB/s on the wire (6 MHz / 8) | 48.8 kB/s (6.5 %); 43.9 kB/s over the whole 11.659 ms window (5.9 %) |

The 1.2 ns (0.7 %) difference at 6 MHz is the 25 ns timestamp grid: every one of the 527 bytes in window 2
spans exactly 1.175 us from the first to the last rising edge, where 7 periods of 166.7 ns are 1.1667 us.
It is not a clock error. The time between bytes is where the time goes:

| Gap (last rising edge of a byte to the first of the next) | Value |
|-----------------------------------------------------------|-------|
| inside one transfer call, 375 kHz | 20.43 us, constant |
| inside one transfer call, 6 MHz | 16.4 to 19.4 us, mean 17.0 us |
| between two transfer calls, 6 MHz window (one-byte polls and 32-byte blocks) | 97.7 to 107.2 us, mean 98.6 us at the 32-byte boundaries |

The structure of window 2 shows the calls: 6 bytes (command), 7 single-byte polls (R1 and the wait for the
token), 16 calls of 32 bytes (the 512-byte block, `SPI_CHUNK` in `plat_spi_hal.c`) and a 2-byte call (CRC).
The 6 MHz clock is therefore used for 6.5 % of the time during the block. The software around the transfer
dominates: 512 bytes x about 17 us is 8.7 ms of the 10.5 ms. Raising the clock would not make the read
faster; transferring the block with fewer or lighter calls would. Whether the chip needs as long per byte
as the simulated CPU is **not known** (section 9).

Transactions and the time between them:

| Quantity | Value |
|----------|-------|
| CS low to the first SCK edge | 44.5 us (window 1), 43.5 us (window 2) |
| last SCK edge to CS high | about 81 us (window 1), about 75 us (window 2) |
| CS high to the 0xFF release byte | about 32 us in both |
| window 1 end to window 2 start (time between transactions) | 163.6 us (18.142375 to 18.305975 ms) |
| CMD0 start to start of its R1 byte | 441.4 us (poll spacing 122.6 us) |
| CMD17 start to the data token | 817 us |
| whole SD check, first power-up clock to last release byte | 16.429 ms (13.5694 to 29.9979) |

## 5 PWM (D5 and D6)

Calculated: TIM3 prescaler 11 gives a 1 MHz count, ARR 19999 gives a period of 20000 us, CCR is the width in us.
The servo driver maps 0 to 180 degrees to 1000 to 2000 us, so 45 degrees is 1250 us.

| Capture | Channel | Pulses | Width measured | Period measured |
|---------|---------|--------|----------------|-----------------|
| boot | D5 and D6 | 4 each | 1500.000 us | 20000.000 us |
| servo | D5 (servo 1) | 11 | 1500 (2 pulses), 1000 (3), 2000 (6) | 20000.000 us |
| servo | D6 (servo 2) | 11 | 1500 (8), 1250 (3) | 20000.000 us |

Observations:

- Two different widths on the same channel (1000 and 2000 us, plus 1500) and two channels with different widths
  at the same time (D5 at 2000 and D6 at 1250 from 183.9243 ms) are visible, with a common period: the
  channels share one counter, so their rising edges coincide in every period (23.924325 ms, 43.924325 ms, ...).
- The first pulse in every capture rises at 23.924325 ms. TIM3 is therefore started at about 3.924 ms and
  the first pulse comes one full period later (**hypothesis**: the compare value is preloaded and takes effect
  at the first update event; the code was not checked for this).
- Latency from the console command to the pin: the reply line of the command starts at 46.9534, 112.6743 and
  178.4253 ms and the first pulse with the new width rises at 63.9243, 123.9243 and 183.9243 ms, i.e. after
  16.97, 11.25 and 5.50 ms. The new width appears at the next period boundary (preload), so the latency is
  between 0 and 20 ms depending on the phase. No pulse of an intermediate width exists in the captures.
  The reply is printed after the compare value is written, so the true latency from the write is slightly longer than
  these numbers; the write itself is not visible on any channel.

Datasheet side: the 1 to 2 ms pulse at 50 Hz is the usual hobby-servo convention; the servo datasheet is not
in the repository, and the real limits of the servo that will be bought are **to confirm** (they decide whether
1000 and 2000 us are the ends of the travel).

## 6 ADC (D7, the service marker)

The ADC has no digital traffic. The firmware raises PB3 for one channel read, so D7 shows when a measurement happens
and how long the firmware spends on it.

| Burst | Start | Pulse widths (us) | Gaps (us) | Length |
|-------|-------|-------------------|-----------|--------|
| self-test (every capture) | 6.6596 ms | 139.825, 139.925, 139.925, 139.925 | 7.250 (three) | 581.35 us |
| `adc` command (adc.vcd) | 50.6639 ms | 142.400, 139.925, 139.925, 139.925 | 7.250 (three) | 583.92 us |

- Four pulses per burst: the four channels IN0, IN1, IN18, IN19, one conversion each (the driver reconfigures the
  sequencer to one channel per conversion, the fix for the scan overrun in the problem log).
- Calculated hardware conversion: 14 ADC clock cycles at 12 MHz = 1.17 us. The marker is high for 139.9 us, 120 times
  longer. It is set and cleared by the firmware in `plat_adc_read()` (`plat_adc_hal.c`): PB3 goes high before
  `convert()` and low after it, so the pulse covers the four `HAL_ADC_ConfigChannel` calls that select one channel,
  `HAL_ADC_Start`, `HAL_ADC_PollForConversion`, `HAL_ADC_GetValue` and `HAL_ADC_Stop`, not the conversion alone.
  The marker therefore shows the moment and the period of the measurements (once per self-test and once
  per `adc` command, not periodically), but it cannot show the conversion time.
- The line `SELFTEST adc OK raw=1025,2050,3074,4095` starts 854.0 us after the last pulse (856.5 us for
  the `adc` command), this is the work between the last conversion and the first character of the line (value conversion and
  formatting); its parts are not separated.
- Values 1025, 2050, 3074, 4095 are the potentiometer positions rounded to 12 bits: the simulated pots stand at
  256, 512, 768 and 1023 of 1023 (about 25, 50, 75 and 100 %), and 256/1023 x 4095 = 1024.8.

## 7 Delay between a firmware event and the reaction on the bus

| Event in the firmware | Reaction on the bus | Delay |
|-----------------------|---------------------|-------|
| reset | USART2 TX goes idle high | 1.8437 ms |
| reset | first character of the banner (start bit) | 1.8957 ms |
| reset | PB9 (CS) driven high | 3.8473 ms (before that D3 reads low, section 8.2) |
| end of the banner line (stop bit) | PB9 (CS) driven high | 171.9 us |
| end of the banner line (stop bit) | TIM3 started (inferred from the first pulse, section 5) | about 248.9 us |
| end of the `spi OK` line (stop bit) | first SCK edge of the SD power-up | 0 within the 25 ns grid (section 8.3) |
| CS low | first SCK edge | 44.5 us, 43.5 us |
| last SD clock of the self-test | first start bit of `SELFTEST sd ...` | 651.6 us |
| last SD clock of `nosd` | first start bit of `SELFTEST sd FAIL ...` | 342.9 us |
| console command `servo` (reply) | first pulse with the new width | 16.97, 11.25, 5.50 ms |

What cannot be measured here: the delay from the arrival of a console character to its reply. The RX line (PA3) is
not one of the eight channels, so the capture has no time reference for the arrival of a command.

## 8 Anomalies

### 8.1 Deliberate: no SD card (nosd.vcd)

Setup: the run without the card. Expected: no answer to CMD0. Seen on the bus:

- CS falls at 14.752625 ms. Five CMD0 frames `40 00 00 00 00 95` follow, each with one more byte read after
  it, starting at 14.7971, 15.2491, 15.7010, 16.1554 and 16.6073 ms (452 to 454 us apart), all inside one CS-low
  window of 35 bytes. CS rises at 17.024275 ms.
- MISO (D2) has no edge at all in the capture. It is 0 for every one of the 35 bytes (a card with a pulled-up
  line would give 0xFF).
- 342.9 us after the last SPI clock the UART prints `SELFTEST sd FAIL step=init status=3`, and the report
  ends with `total=5 passed=4 failed=1`. The other checks are not affected: the servo checks pass and the
  PWM starts at the same time as in the other captures (first pulse at 23.924325 ms).

Reading: the byte after each CMD0 is 0x00. Its top bit is 0, so for the driver it is formally an R1
response, but not the expected 0x01 (idle), so the driver repeats CMD0 five times (`SDCARD_CMD0_TRIES`) and
gives up with PLAT_ERR_BUS (3). On a line that stays high, with a pull-up or floating high, the bus would carry 0xFF for all
eight polls, `command()` would return TIMEOUT, and after the five tries `sdcard_init` would end with
status 2 (TIMEOUT) instead: the other branch of the driver (`go_idle` in `sdcard.c`). What the platform layer returns to the
driver for each of these transfers is PLAT_OK; only the bytes on the wire show the problem, which is the kind
of mismatch between HAL and bus that the lab text asks to find.

The same bytes are replayed into the driver in `tests/test_sd_replay.c`
(`test_card_that_never_answers_idle_gives_bus_error`): `sdcard_init` returns PLAT_ERR_BUS, the driver
consumes exactly the 46 recorded bytes, and the MOSI bytes it sends are identical to the ones on the bus.

The "SD card (SPI mode)" decoder of sigrok decodes the first CMD0 correctly and then mis-reads the following ones
(it prints argument 0x0095 and CRC7 0x7F for the second), because it follows the card's answers and there are
none. `vcd_tool.py` decodes all five frames, because it works on the MOSI bytes.

### 8.2 Found in the captures: CS low and MISO low at start-up

Measured:

- D3 (PB9, the chip select) reads 0 from t = 0 until it rises at 3.847325 ms. The simulator shows every
  unconfigured MCU pin as 0 (D1 and D4 do the same before their peripherals start), so the first part of
  that interval says nothing about the pin.
- D2 (MISO) is high at t = 0, falls at 0.818525 ms and rises again at 3.847325 ms, the same instant as CS
  goes high: 3.029 ms in which the card model drives MISO low.
- It happens once more at the first transaction: MISO falls at 14.752625 ms, the instant CS falls, and rises at
  14.817150 ms after the first byte. The first MISO byte of window 1 is therefore 0x00 and not 0xFF.
  In window 2 (18.305975 ms) and in window 3 of sdread.vcd (51.312425 ms) MISO stays high at CS fall.
- The driver sends CMD0 44.5 us after CS falls. There is no 0xFF byte before the command, so it does not
  wait for MISO to go high first, and the model accepted the command.

Cause of the early low level (read from `firmware/Core/Src/main.c` and `firmware/platform/src/plat_spi_hal.c`):

- `MX_GPIO_Init()`, generated by CubeMX with the output level of PB9 set to low, calls
  `HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_RESET)` and then `HAL_GPIO_Init()`, which makes the pin
  a push-pull output. From then on PB9 drives low, which is consistent with the MISO edge at 0.818525 ms: the card
  model sees itself selected. The firmware drives PB9 high for the first time in `plat_spi_init()` (`plat_spi_cs(0)`),
  which `app_init()` calls after the banner: 3.847325 ms. The fix is the CubeMX setting "GPIO output level: High" for
  PB9 (see [pinout.md](../pinout.md)); the captures in this folder were recorded before it.

Hypothesis (not verified):

- The card model holds MISO low from the moment it is selected until it has received its first byte, and
  only the first selection after power-up does this.

A real card that holds DO low while it is busy would need the host to clock 0xFF bytes until the line is high
before it sends a command (**to confirm** in the specification). That would be a driver improvement; it is not
implemented.

### 8.3 Behaviour that is not explained

- The marker burst starts 25.9 us (3.0 bit times) after the start of the line feed that ends the BEGIN line,
  that is inside the last UART frame. The SD power-up clocks start at 13.5694 ms, which is the end of the stop
  bit of the `spi OK` line to within the time grid. The two results do not fit one simple model of when the
  UART transmit function returns. Possible reasons are in the simulator's USART or in the order of the code
  in the self-test; **not verified**.
- The first marker pulse of the second burst in adc.vcd is 2.5 us longer (142.400 against 139.925 us).
- The 7.23 us pause between UART characters and the 17 us between SPI bytes (sections 3.3 and 4.4).

## 9 What the simulator cannot show, and how to check it on hardware

| Item | In the simulator | On the real board |
|------|------------------|-------------------|
| Edge quality: rise and fall times, ringing, crosstalk on 6 MHz SCK over jumper wires | all edges are ideal; every timestamp is on a 25 ns grid | oscilloscope on SCK, MOSI and MISO at the card socket; if the edges are bad, shorten the wires or lower the clock |
| Setup and hold of MOSI to SCK | MOSI changes at the rising edge (zero setup) | scope or analyzer with at least 100 MS/s on MOSI against SCK, against the card's timing table (**to confirm** in the specification) |
| Floating inputs and pull-ups | MISO reads 0 with no card; the USART TX pin is low before init (D4 at 0 until 1.8437 ms) | repeat the `nosd` test with the card removed and see what the line does; reset-state levels from the datasheet |
| Real CPU and HAL timing (17 us per byte, 100 us per call, 7.2 us between characters) | the simulated CPU decides | repeat the block read on the board with a logic analyzer; only then optimise |
| Card behaviour | always answers after one byte, never busy, no CRC errors, one fixed image | at least two real cards of different makers: response delay (up to 8 bytes, **to confirm**), busy periods, long reads |
| ADC accuracy | ideal pot positions, no noise, no source-impedance error; conversion time not visible | measure noise with a fixed input, check RAIN against the sampling time (open check in `docs/pinout.md`), measure the conversion on D7 with a lighter marker |
| Power | no current limit, no droop | current and 3V3/5V ripple while both servos move and during a card read (scope or current probe); a logic analyzer cannot show it |
| Ground and probes | no ground | common ground between analyzer and board before trusting any capture |
| Servo load | only the PWM waveform | jitter and stalling with a real servo; see the power budget |
| Clock accuracy | the 12 MHz derived clock is exact, so the baud error is exactly the BRR error | the HSI tolerance adds to it: measure the bit time of a real frame |

## 10 Reproduce

```bash
scripts/capture.sh all
scripts/decode_captures.sh
make check            # includes test_sd_replay with the vectors generated from boot.vcd and nosd.vcd
```
