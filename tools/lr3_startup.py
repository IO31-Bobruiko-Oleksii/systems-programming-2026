#!/usr/bin/env python3
"""Start-up time from a capture: reset to the first tracking decision (LR3 section 6.2, item 7).

usage: lr3_startup.py <capture.vcd>

Any capture made with scenarios/lr3.diagram.json (or burst.diagram.json) works, because every
Wokwi capture begins at reset: t = 0 is the start of the simulation. The channels are the ones of
docs/instrumentation.md: D0 loop busy, D1 T2, D4 UART TX, D5 servo 1 PWM, D6 SD chip select,
D7 ADC marker. The UART lines are decoded from D4 by tools/lr3_analyze.py.

The milestones are read from the first events after reset, so the capture does not have to be
made for this purpose. Deadline (docs/tasks.md, T6): the main loop runs within 1 s after reset.
"""
import argparse

import lr3_analyze as A

DEADLINE_MS = 1000.0


def ms(t_ns):
    return t_ns / 1e6


def first(seq):
    return seq[0] if seq else None


def row(name, t_ms, note=""):
    cell = "%10.3f" % t_ms if t_ms is not None else "%10s" % "-"
    return "  %-62s %s ms  %s" % (name, cell, note)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("capture")
    args = ap.parse_args()
    cap = A.Capture(args.capture)

    lines = A.uart_lines(cap)
    busy = cap.pulses(A.BUSY)
    t2 = first(cap.pulses(A.T2))
    adc = cap.pulses(A.ADC)
    pwm = first(cap.pulses(A.PWM))
    cs_lows = cap.lows(A.CS)

    adc_group = []
    for p in adc:
        if adc_group and p[0] - adc_group[-1][1] > 1e6:
            break
        adc_group.append(p)

    selftest = busy[0] if busy else None
    first_pass = busy[1] if len(busy) > 1 else None
    first_tx = lines[0][0] if lines else None
    sd_low = [p for p in cs_lows if p[0] > 0 and (not selftest or p[1] <= selftest[1])]

    print("start-up from %s (t = 0 is the reset, %.3f ms of capture)" % (args.capture, ms(cap.last)))
    print()
    print("MILESTONES")
    print(row("first byte on the UART (start of the boot banner)", ms(first_tx) if first_tx else None))
    if adc_group:
        print(row("first ADC conversion (self-test)", ms(adc_group[0][0])))
        print(row("last of the %d first conversions ends (all channels read once)" % len(adc_group),
                  ms(adc_group[-1][1])))
    if sd_low:
        print(row("first SD chip select low after configuration", ms(sd_low[0][0])))
        print(row("last SD chip select release of the self-test", ms(sd_low[-1][1]),
                  "(init and the read of one block)"))
    print(row("first servo 1 PWM pulse", ms(pwm[0]) if pwm else None,
              "(width %.0f us)" % ((pwm[1] - pwm[0]) / 1e3) if pwm else ""))
    print(row("self-test report finished, first wait for a byte", ms(selftest[1]) if selftest else None,
              "(D0 falls)"))
    print(row("first normal loop pass starts", ms(first_pass[0]) if first_pass else None,
              "(D0 rises again)"))
    print(row("first T2 run starts", ms(t2[0]) if t2 else None))
    print(row("first T2 run ends: first tracking decision", ms(t2[1]) if t2 else None))
    print()

    print("UART LINES UNTIL THE FIRST T2 RUN (start of the first byte)")
    early = False
    for t, text in lines:
        if t2 and t > t2[0]:
            break
        if not text.startswith(("Heliostat", "SELFTEST")):
            early = True
        print("  %10.3f ms  %s" % (ms(t), text))
    if early:
        print("  note: a console line was answered before the first T2 run; a `load` command")
        print("  restarts the 50 ms grid of T2, so the time of the first run depends on the scenario.")
        print("  Use a capture without early commands (stats, stow, burst) for the T2 milestone.")
    print()

    print("VERDICT")
    if selftest and first_pass:
        done = ms(first_pass[0])
        print("  main loop running %.1f ms after reset, deadline %.0f ms: %s"
              % (done, DEADLINE_MS, "ok" if done <= DEADLINE_MS else "MISSED"))
    else:
        print("  no loop pass found in the capture")
    if t2:
        print("  tracking decided %.1f ms after reset" % ms(t2[1]))


if __name__ == "__main__":
    main()
