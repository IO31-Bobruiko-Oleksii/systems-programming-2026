#!/usr/bin/env python3
import argparse
import re
import statistics

DEADLINES = {"late": (25000, "T2 start within 25 ms of its release"),
             "sense": (5000, "T1 within 5 ms"),
             "cmd": (100000, "T4 command execution, reply within 100 ms"),
             "stow": (10000, "E1 servo update within 10 ms")}
STAT = re.compile(r"STAT name=(\w+) n=(\d+) min_us=(\d+) avg_us=(\d+) max_us=(\d+)")
LOAD = re.compile(r"STAT load window_ms=(\d+) busy_ms=(\d+) load_pm=(\d+)")
COUNT = re.compile(r"STAT count runs=(\d+) missed=(\d+) rx_bytes=(\d+) rx_errors=(\d+) passes=(\d+)")
KEYS = re.compile(r"(\w+)=(-?\d+)")
EVT = re.compile(r"EVT stow t_us=(\d+) lat_us=(\d+)")


def records(path):
    with open(path, "rb") as f:
        text = f.read().decode("ascii", "replace").replace("\r", "")
    return [r.strip() for r in re.sub(r"(STAT |EVT |CMD |SELFTEST )", r"\n\1", text).split("\n")
            if r.strip()]


def parse(path):
    out = {"windows": [], "events": [], "fails": [], "log": None, "button": None, "time": [],
           "getok": []}
    cur, pending = None, []
    for r in records(path):
        if r.startswith("CMD stats-reset OK") or r.startswith("CMD load OK us="):
            pending = []
        m = EVT.match(r)
        if m:
            pending.append(int(m.group(2)))
            out["events"].append(int(m.group(2)))
        if "FAIL" in r and not r.startswith("SELFTEST"):
            out["fails"].append(r)
        if r.startswith("CMD log OK") and "seq=" in r:
            out["log"] = dict((k, int(v)) for k, v in KEYS.findall(r))
        if r.startswith("CMD log-get OK"):
            out["getok"].append(dict((k, int(v)) for k, v in KEYS.findall(r))["ok"])
        if r.startswith("CMD button OK"):
            out["button"] = dict((k, int(v)) for k, v in KEYS.findall(r))
        if r.startswith("CMD time OK"):
            out["time"].append(dict((k, int(v)) for k, v in KEYS.findall(r)))
        m = STAT.match(r)
        if m:
            if m.group(1) == "loop":
                cur = {"stat": {}, "events": pending}
                pending = []
            if cur is not None:
                cur["stat"][m.group(1)] = tuple(int(v) for v in m.groups()[1:])
        elif cur is not None and r.startswith("STAT "):
            kind = r.split()[1]
            m = LOAD.match(r) or COUNT.match(r)
            if m:
                cur[kind] = tuple(int(v) for v in m.groups())
            elif kind in ("rx", "mem"):
                cur[kind] = dict((k, int(v)) for k, v in KEYS.findall(r))
            if kind == "mem":
                out["windows"].append(cur)
                cur = None
    return out


def plan_of(path):
    windows, presses = [], []
    with open(path) as f:
        for line in f:
            m = re.match(r"\s*# window (\d+) seconds=(\d+)( pure)?", line)
            if m:
                windows.append((int(m.group(2)), m.group(3) is not None))
            m = re.match(r"\s*# press (\w+) offset=(\S+)", line)
            if m:
                presses.append((m.group(1), None if m.group(2) == "-" else int(m.group(2))))
    return windows, presses


def get(w, *path, default=None):
    v = w
    for p in path:
        try:
            v = v[p]
        except (KeyError, IndexError, TypeError):
            return default
    return v


def stat(w, name, idx):
    """One value of a STAT name=... line; None when the task did not run in the window."""
    s = get(w, "stat", name)
    if s is None or (s[0] == 0 and idx != 0):
        return None
    return s[idx]


def table(wins):
    cols = ["W%d" % (i + 1) for i in range(len(wins))]
    rows = []

    def add(label, fn, limit=None):
        vals = [fn(w) for w in wins]
        verdict = ""
        if limit is not None:
            known = [v for v in vals if v is not None]
            verdict = "" if not known else ("ok" if all(v <= limit for v in known) else "MISSED")
        rows.append((label, vals, "" if limit is None else str(limit), verdict))

    add("window_ms", lambda w: get(w, "load", 0))
    add("T2 runs", lambda w: get(w, "count", 0))
    add("T2 skipped (missed)", lambda w: get(w, "count", 1), 0)
    add("T2 start late, max us", lambda w: stat(w, "late", 3), DEADLINES["late"][0])
    add("T2 start late, avg us", lambda w: stat(w, "late", 2))
    add("T2 period, avg us", lambda w: stat(w, "period", 2))
    add("T2 period, max us", lambda w: stat(w, "period", 3))
    add("T1 sense, max us", lambda w: stat(w, "sense", 3), DEADLINES["sense"][0])
    add("T2 body (track), max us", lambda w: stat(w, "track", 3))
    add("loop pass, avg us", lambda w: stat(w, "loop", 2))
    add("loop pass, max us", lambda w: stat(w, "loop", 3))
    add("T4 commands, n", lambda w: stat(w, "cmd", 0))
    add("T4 command, avg us", lambda w: stat(w, "cmd", 2))
    add("T4 command, max us", lambda w: stat(w, "cmd", 3), DEADLINES["cmd"][0])
    add("T5 log block writes, n", lambda w: stat(w, "logflush", 0))
    add("T5 log block write, max us", lambda w: stat(w, "logflush", 3))
    add("E1 stow, n", lambda w: stat(w, "stow", 0))
    add("E1 stow, avg us", lambda w: stat(w, "stow", 2))
    add("E1 stow, max us", lambda w: stat(w, "stow", 3), DEADLINES["stow"][0])
    add("CPU load, per mille", lambda w: get(w, "load", 2))
    add("rx bytes taken", lambda w: get(w, "count", 2))
    add("rx bytes dropped", lambda w: get(w, "rx", "dropped"), 0)
    add("rx errors", lambda w: get(w, "count", 3), 0)
    add("rx ring max (of 256)", lambda w: get(w, "rx", "ring_max"))
    add("rx interrupt avg, cycles", lambda w: get(w, "rx", "isr_avg"))
    add("stack used, bytes", lambda w: get(w, "mem", "stack_used"))
    add("heap used, bytes", lambda w: get(w, "mem", "heap_used"))
    width = max(len(r[0]) for r in rows)
    lines = ["%-*s %s  %9s  verdict" % (width, "metric", " ".join("%10s" % c for c in cols),
                                        "deadline")]
    for label, vals, limit, verdict in rows:
        cells = " ".join("%10s" % ("-" if v is None else v) for v in vals)
        lines.append("%-*s %s  %9s  %s" % (width, label, cells, limit, verdict))
    return lines


def percentile(vals, p):
    s = sorted(vals)
    return s[min(len(s) - 1, int(round(p / 100.0 * (len(s) - 1))))]


def summary(name, vals):
    if not vals:
        return "  %-12s none" % name
    late = sum(1 for v in vals if v > DEADLINES["stow"][0])
    return "  %-12s n %3d  min %6d  median %6d  p90 %6d  max %6d us  over 10 ms: %d" % (
        name, len(vals), min(vals), int(statistics.median(vals)), percentile(vals, 90), max(vals),
        late)


def phase_lines(events, presses):
    lines = []
    if len(events) != len(presses):
        return ["  the scenario has %d presses, the log %d EVT lines: phases not listed"
                % (len(presses), len(events))]
    by_kind = {}
    for (kind, off), lat in zip(presses, events):
        by_kind.setdefault(kind, []).append((off, lat))
    for kind, items in sorted(by_kind.items()):
        lines.append(summary(kind, [lat for _, lat in items]))
    sd = by_kind.get("sd", [])
    if sd:
        lines.append("")
        lines.append("  press during an sd write: ms after the line was sent -> latency us")
        offsets = sorted({o for o, _ in sd})
        for o in offsets:
            lines.append("    %2d ms: %s" % (o, "  ".join("%6d" % lat for off, lat in sd if off == o)))
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("serial")
    ap.add_argument("--scenario")
    args = ap.parse_args()
    d = parse(args.serial)
    wins = d["windows"]
    plan_w, presses = plan_of(args.scenario) if args.scenario else ([], [])
    print("serial log %s: %d statistics window(s), %d EVT stow line(s), %d FAIL line(s)"
          % (args.serial, len(wins), len(d["events"]), len(d["fails"])))
    for f in d["fails"][:5]:
        print("  FAIL: %s" % f)
    if not wins:
        return
    print("")
    print("\n".join(table(wins)))
    for i, w in enumerate(wins):
        ms = get(w, "load", 0)
        pure = i < len(plan_w) and plan_w[i][1]
        if ms is not None and pure:
            nominal = plan_w[i][0] * 1000
            print("W%d: firmware clock %d ms for %d ms of scenario delays (%+.0f ppm)"
                  % (i + 1, ms, nominal, (ms / nominal - 1) * 1e6))
        if ms is not None:
            print("W%d: T2 runs %s, window_ms / 50 = %.0f" % (i + 1, get(w, "count", 0), ms / 50.0))
    print("")
    print("E1 (stow) latency per window, from the EVT lines:")
    for i, w in enumerate(wins):
        print(summary("W%d" % (i + 1), w["events"]))
    if presses:
        print("")
        print("E1 by kind of press (scenario %s):" % args.scenario)
        print("\n".join(phase_lines(d["events"], presses)))
    if d["button"]:
        b = d["button"]
        text = "button: edges %d, accepted %d, ignored %d" % (b["edges"], b["accepted"], b["ignored"])
        if presses:
            text += " (presses sent: %d)" % len(presses)
        print("")
        print(text)
    if d["log"]:
        print("log: " + ", ".join("%s %d" % kv for kv in d["log"].items()))
    if d["getok"]:
        print("log records read back with `log get`: %d, checksum ok: %d" % (len(d["getok"]),
                                                                           sum(d["getok"])))
    if d["time"]:
        print("time command: %d reads, backsteps max %d, read_cycles %d..%d"
              % (len(d["time"]), max(t["backsteps"] for t in d["time"]),
                 min(t["read_cycles"] for t in d["time"]), max(t["read_cycles"] for t in d["time"])))


if __name__ == "__main__":
    main()
