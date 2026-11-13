#!/usr/bin/env python3
"""Checks the T3 (park) probes of scenarios/park.yaml against the serial log.

usage: lr3_park.py <serial.log> --scenario <park.yaml>

The scenario has the comment lines "# probe <before|after|cloud> cycle=<n> offset=<ms>", one in
front of each `track` command. The n-th "CMD track OK" line of the log belongs to the n-th comment.
offset is the time from the step that put the light out (or back) to the step that sent `track`.

  before  920 ms after the light went out: at most 19 dark samples, so parked must be 0
  after  1030 ms after the light went out: the 20th dark sample is done, parked must be 1 and
         the panel at the safe position (zero_az, zero_tilt)
  cloud   the light was out for 900 ms and is back for 100 ms: parked 0 and the dark count 0
"""
import argparse
import re
import sys
from collections import Counter

TRACK = re.compile(
    r"CMD track OK az=(\d+) tilt=(\d+) band=(\d+) zero=(\d+),(\d+) parked=(\d+) stowed=(\d+)"
    r" dark=(\d+) runs=(\d+) missed=(\d+) errors=(\d+)")
PROBE = re.compile(r"#\s*probe (before|after|cloud) cycle=(\d+) offset=(\d+)")
DEADLINE_MS = 1100
SAMPLES = 20
PERIOD_MS = 50


def read_probes(path):
    probes = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = PROBE.search(line)
            if m:
                probes.append((m.group(1), int(m.group(2)), int(m.group(3))))
    return probes


def read_replies(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    # the serial log may lose the line break after a line: search the whole text
    keys = ("az", "tilt", "band", "zero_az", "zero_tilt", "parked", "stowed", "dark", "runs",
            "missed", "errors")
    return [dict(zip(keys, map(int, m.groups()))) for m in TRACK.finditer(text)]


def check(kind, r):
    if kind == "before":
        return r["parked"] == 0 and r["dark"] < SAMPLES
    if kind == "after":
        return (r["parked"] == 1 and r["dark"] == SAMPLES and r["az"] == r["zero_az"]
                and r["tilt"] == r["zero_tilt"])
    return r["parked"] == 0 and r["dark"] == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("serial")
    ap.add_argument("--scenario", required=True)
    args = ap.parse_args()
    probes = read_probes(args.scenario)
    replies = read_replies(args.serial)
    print("park probes: %d in the scenario, %d `CMD track OK` lines in the log"
          % (len(probes), len(replies)))
    if not probes or len(probes) != len(replies):
        print("the numbers differ: the run is not complete")
        return 1
    rows = {"before": [], "after": [], "cloud": []}
    cycles = {}
    for (kind, cycle, offset), r in zip(probes, replies):
        r["offset"] = offset
        r["ok"] = check(kind, r)
        rows[kind].append(r)
        cycles.setdefault(cycle, {})[kind] = r

    print()
    print("cycle  before(%s ms)         after(%s ms)             cloud"
          % (rows["before"][0]["offset"], rows["after"][0]["offset"]))
    print("       dark parked az/tilt   dark parked az/tilt   dark parked az/tilt   runs b->a")
    for c in sorted(cycles):
        b, a, k = cycles[c]["before"], cycles[c]["after"], cycles[c]["cloud"]
        print("%5d  %4d %6d %3d/%-3d %s  %4d %6d %3d/%-3d %s  %4d %6d %3d/%-3d %s  %d"
              % (c, b["dark"], b["parked"], b["az"], b["tilt"], "ok " if b["ok"] else "BAD",
                 a["dark"], a["parked"], a["az"], a["tilt"], "ok " if a["ok"] else "BAD",
                 k["dark"], k["parked"], k["az"], k["tilt"], "ok " if k["ok"] else "BAD",
                 a["runs"] - b["runs"]))

    print()
    n = len(cycles)
    for kind, text in (("before", "parked=0 and fewer than 20 dark samples"),
                       ("after", "parked=1, 20 dark samples, panel at the safe position"),
                       ("cloud", "parked=0 and dark count 0")):
        good = sum(1 for r in rows[kind] if r["ok"])
        print("%-6s %d of %d: %s" % (kind, good, n, text))
    print("dark count seen at the `before` probe: %s"
          % ", ".join("%d x %d" % (v, c) for v, c in sorted(Counter(r["dark"] for r in rows["before"]).items())))
    steps = Counter(a["runs"] - b["runs"] for b, a in zip(rows["before"], rows["after"]))
    print("T2 runs between the two probes (%d ms apart): %s"
          % (rows["after"][0]["offset"] - rows["before"][0]["offset"],
             ", ".join("%d x %d" % (v, c) for v, c in sorted(steps.items()))))
    last = replies[-1]
    print("T2 missed at the end: %d, track errors %d" % (last["missed"], last["errors"]))

    lo = rows["before"][0]["offset"]
    hi = rows["after"][0]["offset"]
    ok = all(r["ok"] for kind in rows for r in rows[kind])
    print()
    print("the 20th dark sample is taken %d..%d ms after the light goes out (theory: 19 periods"
          " of %d ms plus 0..%d ms of phase)" % ((SAMPLES - 1) * PERIOD_MS, SAMPLES * PERIOD_MS,
                                              PERIOD_MS, PERIOD_MS))
    if not ok:
        print("a probe did not find the expected state (BAD above): the deadline is NOT PROVEN")
        return 1
    print("measured: the panel was not parked %d ms after the light went out and was parked at %d ms"
          % (lo, hi))
    print("deadline %d ms: met, margin at least %d ms" % (DEADLINE_MS, DEADLINE_MS - hi))
    return 0


if __name__ == "__main__":
    sys.exit(main())
