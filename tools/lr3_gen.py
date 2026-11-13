#!/usr/bin/env python3
"""Writes the long LR3 scenarios (S1, S2, S3) for the Wokwi CLI.

usage: lr3_gen.py <s1|s2|s3|e1|burst>

The scenario is written to scenarios/<name>.yaml. The comment lines "# window" and "# press"
in the file are read back by lr3_stats.py, so do not edit them.

A Wokwi CLI session of the free plan ends after 5 minutes of real time, and every scenario step
costs about 0.7 s of it. So the scenarios use few steps: the commands of 4 s are sent in one
write (one command per second on average) and no step waits for a reply except the last one.
"""
import random
import sys
from pathlib import Path

NAMES = {"s1": "s1-idle", "s2": "s2-nominal", "s3": "s3-worst", "e1": "e1-idle", "burst": "burst"}
SELFTEST = "SELFTEST END total=5 passed=5 failed=0"
SETTLE_MS = 6025                    # the servos reach their end stops, as in the other scenarios
S1_SECONDS = 250
TICK_MS = 4000
TICKS = {"s2": 32, "s3": 9}
POT_CYCLE = (0.9, 0.5, 0.1, 0.5)    # light on the TR channel, one step per tick
COMMANDS = ("track", "adc", "help", "button", "log")     # all read-only and short
GROUP = 4                           # commands per tick
SD_OFFSETS = tuple(range(3, 28, 2))  # button press, ms after the sd write line was sent
SD_PRESSES = 3                      # per tick in S3
BURST = 20
E1_PRESSES = 40
E1_LEAD_MS = 100
E1_PAUSE_MS = 250
TIME_CALLS = 3
CHUNK_MS = 10000


class Scenario:
    def __init__(self):
        self.lines = []
        self.steps = 0
        self.ms = 0

    def comment(self, text):
        self.lines.append("  # " + text)

    def write(self, *lines):
        text = "".join(line + "\\n" for line in lines)
        self.lines.append('  - write-serial: "%s"' % text)
        self.steps += 1

    def wait(self, text):
        self.lines.append("  - wait-serial: '%s'" % text)
        self.steps += 1

    def delay(self, ms):
        while ms > 0:
            part = min(ms, CHUNK_MS)
            self.lines.append("  - delay: %dms" % part)
            self.steps += 1
            self.ms += part
            ms -= part

    def control(self, part, name, value):
        self.lines += ["  - set-control:", "      part-id: %s" % part,
                       "      control: %s" % name, "      value: %s" % value]
        self.steps += 1


def press(s, tag, offset=None):
    s.comment("press %s offset=%s" % (tag, "-" if offset is None else offset))
    s.control("btn1", "pressed", 1)
    s.delay(10)
    s.control("btn1", "pressed", 0)
    s.delay(250)                    # more than the 200 ms lockout


def press_in_sd_write(s, offset):
    s.write("sd write 16000")
    s.delay(offset)
    press(s, "sd", offset)


def tick(s, t, key, rnd, offsets):
    s.control("pot2", "position", POT_CYCLE[t % len(POT_CYCLE)])
    s.write(*[COMMANDS[(t * GROUP + i) % len(COMMANDS)] for i in range(GROUP)])
    if key == "s2":
        if t % 8 == 3:
            press(s, "nominal")
            s.write("resume")
    else:
        s.delay(100)                # the replies of the group are out before the next line
        if t % 3 == 0:
            s.write("sd write 16000")
            s.delay(100)
        elif t % 3 == 1:
            s.delay(rnd.randrange(0, 900))
            press(s, "random")
        else:
            s.comment("burst %d" % BURST)
            s.write(*["track"] * BURST)
            s.delay(300)
        for _ in range(SD_PRESSES):
            press_in_sd_write(s, offsets.pop(0))
        s.write("resume")
    s.delay(TICK_MS)


def build_e1():
    """E1 in an idle system: presses at random phases, then a window with long `time` commands."""
    s = Scenario()
    rnd = random.Random(2026)
    leads = [rnd.randrange(1, E1_LEAD_MS) for _ in range(E1_PRESSES)]
    s.wait(SELFTEST)
    s.delay(SETTLE_MS)
    s.comment("window 1 seconds=%d" % ((sum(leads) + E1_PRESSES * (E1_PAUSE_MS + 10)) // 1000))
    s.write("stats reset")
    s.delay(50)
    for i, lead in enumerate(leads):
        s.comment("press idle offset=%d" % lead)
        s.delay(lead if i == 0 else E1_PAUSE_MS + lead)
        s.control("btn1", "pressed", 1)
        s.delay(10)
        s.control("btn1", "pressed", 0)
    s.delay(50)
    s.comment("window 2 seconds=1")
    s.write("resume", "stats", "stats reset", *["time"] * TIME_CALLS, "stats", "button")
    s.wait("CMD button OK edges=")
    s.delay(5)
    return s


def build_burst():
    """One `track` line alone, then BURST of them back to back: time from each line to its reply."""
    s = Scenario()
    s.wait(SELFTEST)
    s.delay(500)
    s.write("track")
    s.delay(300)
    s.write(*["track"] * BURST)
    s.delay(600)
    return s


def build(key):
    if key == "e1":
        return build_e1()
    if key == "burst":
        return build_burst()
    s = Scenario()
    s.wait(SELFTEST)
    if key == "s1":
        s.delay(SETTLE_MS)
        s.comment("window 1 seconds=%d pure" % S1_SECONDS)    # only delays: the clocks can be compared
        s.write("stats reset")
        s.wait("CMD stats-reset OK")
        s.delay(S1_SECONDS * 1000)
        s.write("stats")
        s.wait("CMD stats OK")
        s.delay(5)
        return s
    ticks = TICKS[key]
    for pot in ("pot1", "pot2", "pot3", "pot4"):
        s.control(pot, "position", 0.5)
    s.write("stow", "resume", "log on")
    s.delay(100)
    s.comment("window 1 seconds=%d" % (ticks * TICK_MS // 1000))
    s.write("stats reset")
    s.delay(50)
    rnd = random.Random(2026)
    offsets = [SD_OFFSETS[i % len(SD_OFFSETS)] for i in range(ticks * SD_PRESSES)]
    for t in range(ticks):
        tick(s, t, key, rnd, offsets)
    # about 10 records per second are logged: the three records read back are inside the log
    s.write("stats", "log flush", "log", "log get 0", "log get %d" % (ticks * 10),
            "log get %d" % (ticks * 20), "button")
    s.wait("CMD button OK edges=")
    s.delay(5)
    return s


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in NAMES:
        sys.exit(__doc__)
    key = sys.argv[1]
    s = build(key)
    if key in ("e1", "burst"):
        seconds = s.ms // 1000
    else:
        seconds = S1_SECONDS if key == "s1" else TICKS[key] * TICK_MS // 1000
    path = Path(__file__).resolve().parent.parent / "scenarios" / (NAMES[key] + ".yaml")
    head = ["name: '%s: %d s (LR3)'" % (NAMES[key], seconds), "version: 1", "steps:"]
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(head + s.lines) + "\n")
    print("%s: %d steps, %d lines" % (path, s.steps, len(s.lines)))


if __name__ == "__main__":
    main()
