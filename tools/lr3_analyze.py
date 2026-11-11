#!/usr/bin/env python3
import argparse
import bisect
import re
import sys

BUSY, T2, EVENT, BUTTON, TX, PWM, CS, ADC = "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7"
BIT_NS = 1e9 / (12_000_000 / 104)
BYTE_US = 10 * BIT_NS / 1000


class Capture:
    def __init__(self, path):
        self.path = path
        ids = {}
        raw = {}
        t = 0
        self.last = 0
        in_defs = True
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                if in_defs:
                    m = re.match(r"\$var\s+\w+\s+\d+\s+(\S+)\s+(\S+)\s+\$end", line)
                    if m:
                        ids[m.group(1)] = m.group(2)
                        raw[m.group(2)] = []
                    elif line.startswith("$enddefinitions"):
                        in_defs = False
                    continue
                if line[0] == "#":
                    t = int(line[1:])
                    self.last = t
                elif line[0] in "01" and line[1:] in ids:
                    raw[ids[line[1:]]].append((t, int(line[0])))
        missing = [c for c in (BUSY, T2, EVENT, BUTTON, TX, PWM, CS, ADC) if c not in raw]
        if missing:
            sys.exit(f"{path}: channels {missing} are missing. Was the capture made with "
                     f"scenarios/lr3.diagram.json (tools/lr3_capture.sh)?")
        self.tr = {}
        for c, seq in raw.items():
            out = []
            for tt, v in seq:
                if not out or out[-1][1] != v:
                    out.append((tt, v))
            self.tr[c] = out
        self._t = {c: [x[0] for x in v] for c, v in self.tr.items()}

    def value_at(self, c, t):
        i = bisect.bisect_right(self._t[c], t) - 1
        return self.tr[c][i][1] if i >= 0 else 0

    def edges(self, c, val):
        return [t for i, (t, v) in enumerate(self.tr[c]) if v == val and i > 0]

    def pulses(self, c):
        out = []
        tr = None
        for t, v in self.tr[c]:
            if v == 1:
                tr = t
            elif tr is not None:
                out.append((tr, t))
                tr = None
        return out

    def lows(self, c):
        out = []
        tf = None
        for t, v in self.tr[c]:
            if v == 0:
                tf = t
            elif tf is not None:
                out.append((tf, t))
                tf = None
        return out


def uart_lines(cap):
    falls = cap.edges(TX, 0)
    rises = cap.edges(TX, 1)
    if not rises:
        return []
    # the line is low until the USART is switched on; decoding starts when it is idle (high)
    t_idle = 0 if cap.tr[TX][0][1] == 1 else rises[0]
    out, cur, t0, t_ok = [], [], None, 0
    for tf in falls:
        if tf < t_idle or tf < t_ok or cap.value_at(TX, tf + BIT_NS / 2) != 0:
            continue
        b = 0
        for k in range(8):
            if cap.value_at(TX, tf + BIT_NS * (1.5 + k)):
                b |= 1 << k
        t_ok = tf + BIT_NS * 9.5
        if t0 is None:
            t0 = tf
        if b == 10:
            out.append((t0, bytes(x for x in cur if x not in (10, 13)).decode("ascii", "replace")))
            cur, t0 = [], None
        else:
            cur.append(b)
    return out


def summ(values):
    if not values:
        return None
    return {"n": len(values), "min": min(values), "avg": sum(values) / len(values), "max": max(values)}


def fmt(s, scale=1000.0, nd=1):
    if s is None:
        return "no data"
    return (f"n={s['n']:<5d} min={s['min'] / scale:10.{nd}f}  avg={s['avg'] / scale:10.{nd}f}  "
            f"max={s['max'] / scale:10.{nd}f} us")


def parse_serial(path):
    stats, load, count, evts = {}, None, None, []
    with open(path, "rb") as f:
        text = f.read().decode("ascii", "replace").replace("\r", "")
        for line in re.sub(r"(STAT |EVT |CMD )", r"\n\1", text).split("\n"):
            m = re.match(r"STAT name=(\w+) n=(\d+) min_us=(\d+) avg_us=(\d+) max_us=(\d+)", line)
            if m:
                stats[m.group(1)] = {"n": int(m.group(2)), "min": int(m.group(3)) * 1000.0,
                                     "avg": int(m.group(4)) * 1000.0, "max": int(m.group(5)) * 1000.0}
                continue
            m = re.match(r"STAT load window_ms=(\d+) busy_ms=(\d+) load_pm=(\d+)", line)
            if m:
                load = tuple(int(x) for x in m.groups())
                continue
            m = re.match(r"STAT count runs=(\d+) missed=(\d+) rx_bytes=(\d+) rx_errors=(\d+) passes=(\d+)", line)
            if m:
                count = tuple(int(x) for x in m.groups())
                continue
            m = re.match(r"EVT stow t_us=(\d+) lat_us=(\d+)", line)
            if m:
                evts.append((int(m.group(1)), int(m.group(2))))
    return stats, load, count, evts


def row(p, label, fw, an, note=""):
    p(f"  {label:<26} {fw:<34} {an:<34} {note}")


def stat_cell(s):
    if s is None:
        return "-"
    return f"{s['min'] / 1000:.1f} / {s['avg'] / 1000:.1f} / {s['max'] / 1000:.1f}"


def inside(t, lo, hi):
    return lo <= t < hi


def overlap(pulses, lo, hi):
    return sum(max(0, min(b, hi) - max(a, lo)) for a, b in pulses)


def report_loop(cap, p, fw, lo, hi, period_ms, p_limit_us):
    stats, load, count, _ = fw
    win_ms = (hi - lo) / 1e6
    p(f"window {lo / 1e6:.3f} .. {hi / 1e6:.3f} ms ({win_ms:.1f} ms)")
    p("")

    busy = [(a, b) for a, b in cap.pulses(BUSY) if inside(a, lo, hi)]
    t2 = [(a, b) for a, b in cap.pulses(T2) if inside(a, lo, hi)]
    adc = [(a, b) for a, b in cap.pulses(ADC) if inside(a, lo, hi)]
    if not busy:
        p("no pulse on D0 (loop busy) in the window: is the PB6 probe wired and is plat_probe_init called?")
        return
    rises = [a for a, _ in busy]
    period = [b - a for a, b in zip(rises, rises[1:])]
    with_t2 = set()
    for a, b in t2:
        i = bisect.bisect_right(rises, a) - 1
        if i >= 0:
            with_t2.add(i)
    plain = [busy[i][1] - busy[i][0] for i in range(len(busy)) if i not in with_t2]
    short = [w for w in plain if w <= p_limit_us * 1000]
    long_ = [w for w in plain if w > p_limit_us * 1000]
    sp = summ(period)
    sw = summ(short)

    p("LOOP PASS (D0: rising edge to rising edge = one pass; high width = work of one pass outside the UART wait)")
    p(f"  pass period      {fmt(sp)}")
    p(f"  pass work P      {fmt(sw, nd=2)}   (passes without a T2 run and up to {p_limit_us:.0f} us)")
    if long_:
        p(f"  longer passes without a T2 run: {len(long_)} (commands, log flushes), "
          f"{min(long_) / 1000:.0f} .. {max(long_) / 1000:.0f} us")
    if sw:
        p(f"  byte time {BYTE_US:.1f} us, so the margin before a received byte is lost "
          f"= {BYTE_US - sw['max'] / 1000:.1f} us (largest P), {BYTE_US - sw['avg'] / 1000:.1f} us (average P)")
        p("  P includes the two stores of the probe itself (about 1 us)")
    p("")

    pw = summ([b - a for a, b in t2])
    pp = summ([b[0] - a[0] for a, b in zip(t2, t2[1:])])
    phase = None
    if len(t2) >= 2:
        t0 = t2[0][0]
        nom = period_ms * 1e6
        ph = [x - (t0 + round((x - t0) / nom) * nom) for x, _ in t2]
        phase = max(ph) - min(ph)
    p("TASK T2 (D1)")
    p(f"  start to start   {fmt(pp)}")
    p(f"  pulse width      {fmt(pw)}")
    if phase is not None:
        p(f"  start jitter (largest minus smallest offset from the ideal 50 ms grid): {phase / 1000:.1f} us")

    sense = []
    for a, b in t2:
        mine = [(x, y) for x, y in adc if a <= x and y <= b]
        if mine:
            sense.append(mine[-1][1] - mine[0][0])
    ss = summ(sense)
    n_adc = [len([1 for x, y in adc if a <= x and y <= b]) for a, b in t2]
    p("T1 SENSE (D7: from the start of the first to the end of the fourth conversion)")
    p(f"  burst length     {fmt(ss)}   (conversions per run: {sorted(set(n_adc))})")
    p("")

    t2_total = overlap(cap.pulses(T2), lo, hi)
    busy_total = overlap(cap.pulses(BUSY), lo, hi)
    p("CPU TIME")
    p(f"  T2 pulses add up to {t2_total / 1e6:.2f} ms = {1000.0 * t2_total / (hi - lo):.1f} permille of the window")
    p(f"  loop-busy pin high  {busy_total / 1e6:.2f} ms = {1000.0 * busy_total / (hi - lo):.1f} permille "
      f"(all work outside the UART wait, the per-pass cost P included)")
    p("")

    if stats or load:
        p("COMPARISON with the firmware (STAT lines, cycle counter), all in us unless noted: min / avg / max")
        row(p, "metric", "firmware (cycle counter)", "logic analyzer", "difference")
        rows = []
        if "loop" in stats:
            rows.append(("loop pass period", stats["loop"], sp, "the firmware counts passes with tracking on"))
        if "track" in stats:
            rows.append(("T2 duration", stats["track"], pw, "analyzer pulse starts after the time stamp"))
        if "period" in stats:
            rows.append(("T2 start to start", stats["period"], pp, ""))
        if "sense" in stats:
            rows.append(("T1 sense", stats["sense"], ss, "analyzer: first start to last end only, firmware: whole call"))
        for label, a, b, note in rows:
            d = ""
            if b is not None:
                d = f"avg {(b['avg'] - a['avg']) / 1000:+.1f}, max {(b['max'] - a['max']) / 1000:+.1f}"
            row(p, label, stat_cell(a), stat_cell(b), (d + "  " + note).strip())
        if "loop" in stats and sp:
            row(p, "passes counted", str(stats["loop"]["n"]), str(sp["n"] + 1), "")
        if "late" in stats and phase is not None:
            lj = (stats["late"]["max"] - stats["late"]["min"]) / 1000
            row(p, "T2 start jitter (us)", f"{lj:.1f}", f"{phase / 1000:.1f}", f"difference {phase / 1000 - lj:+.1f}")
        if load:
            row(p, "T2 runs", str(count[0]) if count else "-", str(len(t2)), "")
            row(p, "busy time (ms)", str(load[1]), f"{t2_total / 1e6:.1f} (T2 only)",
                "firmware busy = T2 + commands + log flushes")
            row(p, "load (permille)", str(load[2]), f"{1000.0 * t2_total / (hi - lo):.1f} (T2 only)", "")


def report_event(cap, p, fw):
    _, _, _, evts = fw
    fall = cap.edges(BUTTON, 0)
    clusters = []
    for t in fall:
        if clusters and t - clusters[-1][-1] < 30_000_000:
            clusters[-1].append(t)
        else:
            clusters.append([t])
    ev = cap.pulses(EVENT)
    pwm = cap.pulses(PWM)
    busy = cap.pulses(BUSY)
    cs = cap.lows(CS)
    p(f"button presses found: {len(clusters)} (falling edges on PB5: {len(fall)}), "
      f"EVENT pulses (accepted presses): {len(ev)}, EVT lines in the serial log: {len(evts)}")
    p("")
    k = 0
    for n, cl in enumerate(clusters, 1):
        t0 = cl[0]
        nxt = clusters[n][0] if n < len(clusters) else cap.last
        mine = [(a, b) for a, b in ev if t0 <= a < nxt]
        i = bisect.bisect_right([a for a, _ in busy], t0) - 1
        was_busy = i >= 0 and busy[i][0] <= t0 < busy[i][1]
        p(f"press {n}: first falling edge at {t0 / 1e6:.3f} ms, {len(cl)} falling edge(s) in the bounce, "
          f"loop {'BUSY' if was_busy else 'waiting for a byte'} at the edge")
        if was_busy:
            a, b = busy[i]
            p(f"  the busy stretch that contained the edge: {a / 1e6:.3f} .. {b / 1e6:.3f} ms "
              f"({(b - a) / 1000:.1f} us), edge {(t0 - a) / 1000:.1f} us after its start")
        if not mine:
            p("  no EVENT pulse: the press was ignored by the 200 ms lockout (or is still pending)")
            continue
        a, b = mine[0]
        p(f"  EVENT pulse: starts {(a - t0) / 1000:.1f} us after the edge, ends {(b - t0) / 1000:.1f} us after the edge "
          f"(handler {(b - a) / 1000:.1f} us)")
        if k < len(evts):
            lat = evts[k][1]
            p(f"  firmware EVT line: lat_us={lat} (cycle counter, from the interrupt time stamp to the end of the handler)")
            p(f"  difference analyzer - firmware: {(b - t0) / 1000 - lat:+.1f} us "
              f"(the analyzer starts at the pin edge, the firmware at the time stamp taken in the interrupt)")
            k += 1
        before = [w for w in pwm if w[1] <= t0]
        w_before = (before[-1][1] - before[-1][0]) if before else None
        # a width change of at least 10 us counts as a new command
        after = [w for w in pwm if w[0] >= a and w_before is not None and abs((w[1] - w[0]) - w_before) >= 10_000]
        if w_before is None:
            p("  servo 1: no pulse before the press to compare with")
        elif not after:
            p(f"  servo 1: no pulse with a different width after the press (width before {w_before / 1000:.0f} us)")
        else:
            w = after[0]
            p(f"  servo 1 pulse: {w_before / 1000:.0f} us before, {(w[1] - w[0]) / 1000:.0f} us after; the new pulse starts "
              f"{(w[0] - t0) / 1000:.1f} us and ends {(w[1] - t0) / 1000:.1f} us after the edge "
              f"(task deadline: 30000 us)")
        sd = [x for x in cs if x[0] < b and x[1] > t0 and x[0] > 0]
        if sd:
            p(f"  SD chip select was low around this press: {sd[0][0] / 1e6:.3f} .. {sd[0][1] / 1e6:.3f} ms")


def main():
    ap = argparse.ArgumentParser(description="LR3 logic-analyzer analysis")
    ap.add_argument("vcd")
    ap.add_argument("mode", choices=["loop", "event", "overview"])
    ap.add_argument("--serial", help="serial log of the same run (STAT / EVT lines of the firmware)")
    ap.add_argument("--from-ms", type=float, help="window start (loop mode); default: the UART line 'CMD stats-reset OK'")
    ap.add_argument("--to-ms", type=float, help="window end (loop mode); default: the UART line 'STAT name=loop'")
    ap.add_argument("--period-ms", type=float, default=50.0, help="T2 period of the firmware (default 50)")
    ap.add_argument("--p-limit-us", type=float, default=300.0,
                    help="longest busy stretch still counted as the work of one plain pass (default 300)")
    a = ap.parse_args()

    cap = Capture(a.vcd)
    out = []
    p = out.append
    p(f"capture {a.vcd}: {cap.last / 1e6:.3f} ms")
    p("edges per channel: " + "  ".join(f"{c}={max(len(cap.tr[c]) - 1, 0)}" for c in sorted(cap.tr)))
    p("")
    fw = parse_serial(a.serial) if a.serial else ({}, None, None, [])
    if a.mode == "loop":
        lines = uart_lines(cap)
        lo = hi = None
        for t, text in lines:
            if lo is None and text.startswith("CMD stats-reset OK"):
                lo = t
            if lo is not None and hi is None and t > lo and text.startswith("STAT name=loop"):
                hi = t
        if a.from_ms is not None:
            lo = a.from_ms * 1e6
        if a.to_ms is not None:
            hi = a.to_ms * 1e6
        if lo is None or hi is None:
            p(f"window not found in the UART lines of D4 ({len(lines)} lines decoded); give --from-ms and --to-ms")
            for t, text in lines[:12]:
                p(f"  {t / 1e6:10.3f} ms  {text}")
        else:
            report_loop(cap, p, fw, lo, hi, a.period_ms, a.p_limit_us)
    elif a.mode == "event":
        report_event(cap, p, fw)
    else:
        for t, text in uart_lines(cap):
            p(f"  {t / 1e6:10.3f} ms  {text}")
    print("\n".join(out))


if __name__ == "__main__":
    main()