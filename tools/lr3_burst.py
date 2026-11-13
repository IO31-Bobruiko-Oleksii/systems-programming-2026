#!/usr/bin/env python3
"""Time from a console line to its reply, read from a capture of scenarios/burst.yaml.

usage: lr3_burst.py <capture.vcd> [--serial <serial log>]

The capture has to be made with scenarios/burst.diagram.json: there the channel D3 carries the
receive line PA3 (instead of the button) and D4 the transmit line PA2, so both directions of the
console are on the analyzer. The scenario sends one `track` line alone and then 20 of them back
to back. The firmware sends the reply with the polled transmitter, so the replies queue up.
"""
import argparse
import re

import lr3_analyze as A

RX = "D3"
DEADLINE_US = 100000
BURST = 20
SINGLE = 1


def frames(cap, chan):
    """(start in ns, byte) of every 8N1 frame on the channel."""
    rises = cap.edges(chan, 1)
    if not rises:
        return []
    t_idle = 0 if cap.tr[chan][0][1] == 1 else rises[0]
    out, t_ok = [], 0
    for tf in cap.edges(chan, 0):
        if tf < t_idle or tf < t_ok or cap.value_at(chan, tf + A.BIT_NS / 2) != 0:
            continue
        b = 0
        for k in range(8):
            if cap.value_at(chan, tf + A.BIT_NS * (1.5 + k)):
                b |= 1 << k
        t_ok = tf + A.BIT_NS * 9.5
        out.append((tf, b))
    return out


def lines(frs):
    """(start, end, text) of every line; the end is the end of the line feed frame."""
    out, cur, t0 = [], [], None
    for t, b in frs:
        if t0 is None:
            t0 = t
        if b == 10:
            text = bytes(x for x in cur if x != 13).decode("ascii", "replace")
            out.append((t0, t + 10 * A.BIT_NS, text))
            cur, t0 = [], None
        else:
            cur.append(b)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vcd")
    ap.add_argument("--serial")
    args = ap.parse_args()
    cap = A.Capture(args.vcd)
    cmds = [x for x in lines(frames(cap, RX)) if x[2] == "track"]
    reps = [x for x in lines(frames(cap, A.TX)) if x[2].startswith("CMD track OK")]
    print("capture %s: %.3f ms, %d `track` lines on the receive line, %d replies on the transmit line"
          % (args.vcd, cap.last / 1e6, len(cmds), len(reps)))
    if args.serial:
        with open(args.serial, "rb") as f:
            n = len(re.findall(r"CMD track OK", f.read().decode("ascii", "replace")))
        print("serial log: %d replies" % n)
    n = min(len(cmds), len(reps))
    if n < SINGLE + BURST:
        print("expected %d lines and replies, found %d: the table is partial" % (SINGLE + BURST, n))
    print("")
    print("lines of the burst are sent back to back; reply start / end = time from the end of the")
    print("line to the first / last byte of its reply; the deadline is %d ms" % (DEADLINE_US // 1000))
    print("")
    print("  #    line end   reply bytes   reply start   reply end   verdict (end)")
    ends, starts = [], []
    for k in range(n):
        c0, c1, _ = cmds[k]
        r0, r1, text = reps[k]
        start, end = (r0 - c1) / 1000.0, (r1 - c1) / 1000.0
        starts.append(start)
        ends.append(end)
        label = "single" if k < SINGLE else "%d" % (k - SINGLE + 1)
        print(" %6s %9.3f ms %8d %11.1f us %9.1f us   %s"
              % (label, (c1 - cmds[0][1]) / 1e6, len(text) + 2, start, end,
                 "ok" if end <= DEADLINE_US else "MISSED"))
    burst = ends[SINGLE:]
    bstart = starts[SINGLE:]
    if not burst:
        return
    over = sum(1 for e in burst if e > DEADLINE_US)
    over_s = sum(1 for s in bstart if s > DEADLINE_US)
    print("")
    print("single line: reply starts after %.1f us and ends after %.1f us" % (starts[0], ends[0]))
    print("burst of %d: first reply ends after %.1f us, last after %.1f us" % (len(burst), burst[0], burst[-1]))
    print("burst: replies that end later than 100 ms: %d of %d; start later than 100 ms: %d of %d"
          % (over, len(burst), over_s, len(burst)))
    if len(burst) > 1:
        rb = reps[SINGLE:SINGLE + len(burst)]
        cycle = (rb[-1][1] - rb[0][1]) / (len(rb) - 1) / 1000.0
        size = sum(len(r[2]) + 2 for r in rb) / float(len(rb))
        dur = sum(r[1] - r[0] for r in rb) / len(rb) / 1000.0
        idle = [(b[0] - a[1]) / 1000.0 for a, b in zip(rb, rb[1:])]
        tx = [t for t, _ in frames(cap, A.TX) if rb[0][0] <= t <= rb[-1][1]]
        gaps = sorted(b - a for a, b in zip(tx, tx[1:]) if b - a < 200000)
        print("burst: one reply per %.1f us, end to end" % cycle)
        print("burst: a reply of %.0f bytes takes %.1f us: %.1f us per byte (the median of two frames),"
              " 8N1 at this baud rate needs %.1f us" % (size, dur, gaps[len(gaps) // 2] / 1000.0, A.BYTE_US))
        print("burst: from the end of a reply to the start of the next one: avg %.1f us, min %.1f us"
              % (sum(idle) / len(idle), min(idle)))
    t_lo, t_hi = cmds[SINGLE][0], reps[SINGLE + len(burst) - 1][1]
    t2 = [a for a, _ in cap.pulses(A.T2) if t_lo <= a <= t_hi]
    gaps = [(b - a) / 1e6 for a, b in zip(t2, t2[1:])]
    print("T2 in the burst: %d runs%s" % (len(t2), ", start to start: " + ", ".join(
        "%.1f ms" % g for g in gaps) if gaps else ""))


if __name__ == "__main__":
    main()
