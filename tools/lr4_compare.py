#!/usr/bin/env python3
import os
import re
import sys

ROWS = [
    ("E1 stow, max", "s3-worst", "E1 stow, max us", "us"),
    ("E1 stow, max", "e1-idle", "E1 stow, max us", "us"),
    ("E1 stow, avg", "s3-worst", "E1 stow, avg us", "us"),
    ("T2 start late, max", "s1-idle", "T2 start late, max us", "us"),
    ("T2 start late, max", "s2-nominal", "T2 start late, max us", "us"),
    ("T2 start late, max", "s3-worst", "T2 start late, max us", "us"),
    ("T2 start late, avg", "s3-worst", "T2 start late, avg us", "us"),
    ("T2 period, max", "s3-worst", "T2 period, max us", "us"),
    ("T2 skipped runs", "s3-worst", "T2 skipped (missed)", ""),
    ("T1 sense, max", "s3-worst", "T1 sense, max us", "us"),
    ("T1 sense, max", "e1-idle", "T1 sense, max us", "us"),
    ("T2 body, max (wall clock)", "s3-worst", "T2 body (track), max us", "us"),
    ("T4 command, max", "s3-worst", "T4 command, max us", "us"),
    ("T5 log block write, max", "s3-worst", "T5 log block write, max us", "us"),
    ("rx bytes dropped", "s3-worst", "rx bytes dropped", ""),
    ("rx interrupt, avg cycles", "s3-worst", "rx interrupt avg, cycles", "cyc"),
    ("CPU load, per mille", "s1-idle", "CPU load, per mille", "pm"),
    ("CPU load, per mille", "s2-nominal", "CPU load, per mille", "pm"),
    ("CPU load, per mille", "s3-worst", "CPU load, per mille", "pm"),
    ("main stack used", "s3-worst", "stack used, bytes", "B"),
]


def load(path):
    out = {}
    if not os.path.exists(path):
        return out
    nw = 1
    for ln in open(path):
        m = re.match(r"^(\S.*?)\s{2,}(\S.*)$", ln.rstrip("\n"))
        if not m:
            continue
        if m.group(1) == "metric":
            nw = len([t for t in m.group(2).split() if re.match(r"^W\d+$", t)]) or 1
            continue
        toks = m.group(2).split()
        rest = toks[nw:]
        dl = rest[0] if rest and num(rest[0]) is not None else None
        verdict = rest[1] if len(rest) > 1 else (rest[0] if rest and dl is None else "-")
        out[m.group(1)] = [toks[0], dl, verdict]
    return out


def num(tok):
    try:
        return float(tok)
    except ValueError:
        return None


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    loop_dir, rtos_dir = argv[1], argv[2]
    e1_dir = argv[3] if len(argv) > 3 else rtos_dir
    print("| Metric | Scenario | Loop | FreeRTOS | Difference | PRD limit | Verdict (loop / RTOS) |")
    print("|---|---|---|---|---|---|---|")
    for title, scn, metric, unit in ROWS:
        rd = e1_dir if (scn == "e1-idle" and e1_dir) else rtos_dir
        a = load(os.path.join(loop_dir, "stats-%s.txt" % scn)).get(metric)
        b = load(os.path.join(rd, "stats-%s.txt" % scn)).get(metric)
        if not a or not b:
            print("| %s | %s | n/a | n/a | | | file or metric missing |" % (title, scn))
            continue
        va, vb = num(a[0]), num(b[0])
        lim = num(a[1]) if a[1] is not None else None
        vl = a[2]
        vr = b[2]
        if va is None or vb is None:
            diff = "n/a"
        else:
            rel = ("x%.2f" % (vb / va)) if va else "n/a"
            diff = "%+g %s (%s)" % (vb - va, unit, rel)
        print("| %s | %s | %s | %s | %s | %s | %s / %s |" % (
            title, scn, a[0], b[0], diff, ("%g" % lim) if lim else "-", vl, vr))


main(sys.argv)