# Hardware Components

## GM5528 / GL5528 photoresistor (×4) — target sensor
**Wokwi substitution:** `wokwi-potentiometer` ×4 (no LDR/photoresistor part exists in Wokwi that responds to simulated light; a potentiometer gives the same "variable analog voltage" behavior for the firmware, manually driven by hand instead of by light).

| Field | Value | Datasheet page |
|---|---|---|
| Datasheet | [GL5528](https://passionelectronique.fr/wp-content/uploads/datasheet-photoresistance-LDR-GL5528-CdS.pdf) | page 2, characteristics table |
| Interface | ADC | — |
| Resistance (at 10 lux) | ~10–20 kOhm | p.2 |
| Dark resistance | >= 0.5–1 MOhm | p.2 |
| Spectral peak | 540 nm | p.2 |
| Max voltage / power | 150 V / 100 mW | p.2 |
| Response time (rise/fall) | ~20–45 ms | p.2 |
| Pinout / wiring (Wokwi) | `pot{1-4}:VCC`→`3V3`, `pot{1-4}:GND`→`GND`, `pot{1-4}:SIG`→`PA0`/`PA1`/`PB1`/`PB2` (`ADC1_IN0/IN1/IN18/IN19`) | — |
| Power | ~1–2 mA | p.2 |

## SG92R servo (×2)

| Field | Value | Datasheet page |
|---|---|---|
| Datasheet | [SG92R](http://www.wecl.com.hk/distribution/PDF/Robotics_IoT/58-01-9024.pdf) | full sheet (1 page) |
| Interface | PWM, 50 Hz (20 ms period), 1000–2000 nanosecond pulse width, 1500 nanosecond neutral | — |
| Operating voltage | 4.8–6 V | — |
| Torque / speed | 34.72 oz*in at 4.8V / 0.10 s per 60 degrees at 4.8V | — |
| Rotation range | 180 degrees | — |
| Pinout / wiring | `servo{1,2}:V+`→`5V` (shared), `servo{1,2}:GND`→`GND` (shared), `servo1:PWM`→`PC6` (azimuth, `TIM3_CH1`), `servo2:PWM`→`PC14` (elevation, `TIM3_CH2`) | — |
| Power | ~650 mA | — |

## W25Q64 SPI flash — target component
**Wokwi substitution:** `wokwi-microsd-card` (no generic SPI flash/EEPROM part exists in Wokwi; the microSD card exposes the same SPI protocol — `CS`/`SCK`/`DI`(MOSI)/`DO`(MISO) — so the firmware's SPI driver code is unaffected, only the storage medium and its command set differ.

| Field | Value | Datasheet page |
|---|---|---|
| Datasheet | [W25Q64JV](https://www.winbond.com/resource-files/w25q64jv%20revj%2003272018%20plus.pdf) | p.22 |
| Interface | SPI, mode 0 or mode 3, up to 133 MHz single-I/O | p.11, p.4 |
| Register map | JEDEC ID read `9Fh`, page program `02h`, read data `03h` | p.22 |
| Pinout / wiring (real chip) | `VCC` 3V3, `GND`, `CS`, `CLK`, `DI`(MOSI), `DO`(MISO), `WP#`, `HOLD#` | p.22 |
| Wokwi wiring | `sd1:VCC`→`3V3`, `sd1:GND`→`GND`, `sd1:CS`→`PB9`, `sd1:SCK`→`PA5`, `sd1:DI`→`PA7` (MOSI), `sd1:DO`→`PA6` (MISO), `sd1:CD`→ unconnected | — |
| Wokwi part reference | [wokwi-microsd-card](https://docs.wokwi.com/parts/wokwi-microsd-card) | — |
| Power | active ~4 mA / standby ~1 nanoA | p.4 |