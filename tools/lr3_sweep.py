#!/usr/bin/env python3
import argparse
import bisect
import re

import lr3_analyze as A

E1_DEADLINE_US = 10000
T2_DEADLINE_US = 25000
SRE = r"CMD load OK us=(\d+) ms=(\d+)"
RXRE = r"STAT rx init=(\d+) dropped=(\d+) ring_max=(\d+) errors=(\d+) isr_n=(\d+) isr_avg=(\d+) isr_max=(\d+)"
RXKEYS = ("init", "dropped", "ring_max", "errors", "isr_n", "isr_avg", "isr_max")


def uart_windows(cap):
    out, cur = [], None
    for t, text in A.uart_lines(cap):
        m = re.match(SRE, text)
        if m:
            cur = {"x": int(m.group(1)), "lo": t, "hi": None, "last": None, "replies": []}
            out.append(cur)
        elif cur is not None and cur["hi"] is None:
            if text.startswith("STAT name=loop"):
                cur["last"] = t
            elif text.startswith("CMD resume OK"):
                cur["replies"].append(t)
            elif text.startswith("EVT load done"):
                cur["hi"] = cur["last"]
    return [w for w in out if w["hi"] is not None]


def serial_windows(path):
    with open(path, "rb") as f:
        text = f.read().decode("ascii", "replace").replace("\r", "")
    out, cur = [], None
    for line in re.sub(r"(STAT |EVT |CMD )", r"\n\1", text).split("\n"):
        m = re.match(SRE, line)
        if m:
            cur = {"x": int(m.group(1)), "stats": {}, "count": None, "load": None, "lat": [],
                   "resume": 0, "other": 0, "done": False, "rx": None}
            out.append(cur)
            continue
        if cur is None or cur["done"]:
            continue
        m = re.match(r"STAT name=(\w+) n=(\d+) min_us=(\d+) avg_us=(\d+) max_us=(\d+)", line)
        if m:
            cur["stats"][m.group(1)] = {"n": int(m.group(2)), "max": int(m.group(5))}
        m = re.match(r"STAT load window_ms=(\d+) busy_ms=(\d+) load_pm=(\d+)", line)
        if m:
            cur["load"] = int(m.group(3))
        m = re.match(r"STAT count runs=(\d+) missed=(\d+) rx_bytes=(\d+) rx_errors=(\d+) passes=(\d+)", line)
        if m:
            cur["count"] = tuple(int(v) for v in m.groups())
        m = re.match(RXRE, line)
        if m:
            cur["rx"] = dict(zip(RXKEYS, (int(v) for v in m.groups())))
        m = re.match(r"EVT stow t_us=(\d+) lat_us=(\d+)", line)
        if m:
            cur["lat"].append(int(m.group(2)))
        if line.startswith("EVT load done"):
            cur["done"] = True
        elif line.startswith("CMD resume OK"):
            cur["resume"] += 1
        elif line.startswith("CMD "):
            cur["other"] += 1
    return out


def measure(cap, w):
    lo, hi, x = w["lo"], w["hi"], w["x"]
    busy_all, t2_all = cap.pulses(A.BUSY), cap.pulses(A.T2)
    busy = [(a, b) for a, b in busy_all if lo <= a < hi]
    t2 = [(a, b) for a, b in t2_all if lo <= a < hi]
    rises = [a for a, _ in busy]
    with_t2 = {bisect.bisect_right(rises, a) - 1 for a, _ in t2}
    limit = (x + 300) * 1000
    plain = [b - a for i, (a, b) in enumerate(busy) if i not in with_t2 and b - a <= limit]
    gaps = [b - a for a, b in zip(rises, rises[1:])]
    falls = [t for t in cap.edges(A.BUTTON, 0) if lo <= t < hi]
    groups = []
    for t in falls:
        if groups and t - groups[-1][-1] < 30_000_000:
            groups[-1].append(t)
        else:
            groups.append([t])
    ev = [(a, b) for a, b in cap.pulses(A.EVENT) if lo <= a < hi]
    lat = []
    for n, g in enumerate(groups):
        nxt = groups[n + 1][0] if n + 1 < len(groups) else hi
        mine = [b for a, b in ev if g[0] <= a < nxt]
        if mine:
            lat.append((mine[0] - g[0]) / 1000.0)
    hit = 0
    for r in w["replies"]:
        a, b = r - 400_000 - 48 * A.BYTE_US * 1000, r
        if any(s < b and e > a for s, e in t2):
            hit += 1
    span = hi - lo
    return {"p_avg": sum(plain) / len(plain) / 1000 if plain else None,
            "p_max": max(plain) / 1000 if plain else None,
            "per_avg": sum(gaps) / len(gaps) / 1000 if gaps else None,
            "per_max": max(gaps) / 1000 if gaps else None,
            "busy_pm": 1000.0 * A.overlap(busy_all, lo, hi) / span,
            "t2_n": len(t2), "t2_first": (t2[0][0] - lo) / 1e6 if t2 else None,
            "lat": lat, "hit": hit, "n_rep": len(w["replies"])}


def cell(v, spec, empty="-"):
    return empty if v is None else format(v, spec)


def main():
    ap = argparse.ArgumentParser(description="LR3 breaking-point sweep: one row per load window")
    ap.add_argument("vcd")
    ap.add_argument("--serial", required=True)
    ap.add_argument("--sent", type=int, help="bytes sent to the console in each window (default: the largest rx_bytes seen)")
    ap.add_argument("--control-us", type=int, default=1,
                    help="load value of the control window of sweep-uart (bursts placed on a T2 run), default 1")
    ap.add_argument("--names", default="",
                    help="windows that are not part of the sweep, as X=name,X=name (for example 1=stats-reply,2=sd-write); "
                         "the name is printed on the row and the window stays out of the breaking points")
    a = ap.parse_args()

    cap = A.Capture(a.vcd)
    vw = uart_windows(cap)
    sw = [w for w in serial_windows(a.serial) if w["done"]]
    print(f"capture {a.vcd}: {cap.last / 1e6:.3f} ms, load windows on D4: {len(vw)}, in the serial log: {len(sw)}")
    if len(vw) != len(sw) or any(v["x"] != s["x"] for v, s in zip(vw, sw)):
        print("the windows of the capture and of the serial log do not match; the table is not trustworthy")
    special = {}
    for item in filter(None, a.names.split(",")):
        key, _, name = item.partition("=")
        special[int(key)] = name
    special.setdefault(a.control_us, "control")
    rows = []
    for v, s in zip(vw, sw):
        r = measure(cap, v)
        r.update(x=v["x"], s=s, tag=special.get(v["x"]))
        rows.append(r)
    ring = bool(rows) and all(r["s"]["rx"] for r in rows)
    sent = a.sent
    if sent is None:
        sent = max((r["s"]["count"][2] for r in rows if r["s"]["count"]), default=0)
    if ring:
        have = any(r["s"]["rx"]["isr_n"] for r in rows)
        print("bytes sent per window: counted by the receive interrupt (isr_n); bytes lost: counted by the ring (dropped); "
              f"byte time {A.BYTE_US:.1f} us")
    else:
        have = bool(sent)
        print(f"bytes sent per window: {sent} ({'given' if a.sent is not None else 'assumed: the largest rx_bytes seen'}); "
              f"byte time {A.BYTE_US:.1f} us; control window: load {a.control_us}")
    print("")


    print("UART AND LOAD (P = work of one loop pass outside the UART wait, pin PB6; margin = byte time - largest P)")
    print(f"  {'X us':>6} {'P avg':>7} {'P max':>7} {'margin':>7} {'fw pm':>6} {'pin pm':>7} {'rx':>5} {'lost':>5} "
          f"{'err':>4} {'ok':>3} {'bad':>4} {'T2 in burst':>12}")
    for r in rows:
        s = r["s"]
        c = s["count"]
        rx = c[2] if c and have else None
        lost = None
        if rx is not None:
            lost = s["rx"]["dropped"] if ring else sent - rx
        margin = A.BYTE_US - r["p_max"] if r["p_max"] is not None else None
        print(f"  {r['x']:>6} {cell(r['p_avg'], '7.1f'):>7} {cell(r['p_max'], '7.1f'):>7} {cell(margin, '+7.1f'):>7} "
              f"{cell(s['load'], '6d'):>6} {r['busy_pm']:7.1f} {cell(rx, '5d'):>5} {cell(lost, '5d'):>5} "
              f"{cell(c[3] if c and have else None, '4d'):>4} {cell(s['resume'] if have else None, '3d'):>3} "
              f"{cell(s['other'] if have else None, '4d'):>4} "
              + (f"{r['hit']:>5} of {r['n_rep']:<3}" if have else f"{'-':>12}") + (f"  {r['tag']}" if r["tag"] else ""))
    if ring:
        print("")
        print("RECEIVE BUFFER (STAT rx; arrived = bytes the interrupt took, read = bytes the console took from the ring,")
        print("left = still in the ring at the end of the window; isr times in CPU cycles of the 12 MHz clock)")
        print(f"  {'X us':>6} {'arrived':>8} {'read':>6} {'dropped':>8} {'left':>5} {'ring max':>9} {'errors':>7} "
              f"{'isr avg':>8} {'isr max':>8}")
        for r in rows:
            x = r["s"]["rx"]
            c = r["s"]["count"]
            read = c[2] if c else None
            left = x["isr_n"] - x["dropped"] - read if read is not None else None
            print(f"  {r['x']:>6} {x['isr_n']:>8} {cell(read, '6d'):>6} {x['dropped']:>8} {cell(left, '5d'):>5} "
                  f"{x['ring_max']:>9} {x['errors']:>7} {x['isr_avg']:>8} {x['isr_max']:>8}"
                  + (f"  {r['tag']}" if r["tag"] else ""))
    print("")
    print("DEADLINES (T2: late + duration within 25 ms; E1: firmware reaction within 10 ms)")
    print(f"  {'X us':>6} {'period avg':>11} {'period max':>11} {'T2 runs':>8} {'late max':>9} {'missed':>7} {'T2':>7} "
          f"{'presses':>8} {'lat fw max':>11} {'lat pin max':>12} {'E1':>7}")
    for r in rows:
        s = r["s"]
        c = s["count"]
        late = s["stats"].get("late", {}).get("max")
        trk = s["stats"].get("track", {}).get("max")
        t2v = "-"
        if late is not None and trk is not None and c:
            t2v = "MISSED" if (c[1] > 0 or late + trk > T2_DEADLINE_US) else "ok"
        e1v = "-"
        if s["lat"]:
            e1v = "MISSED" if max(s["lat"]) > E1_DEADLINE_US else "ok"
        print(f"  {r['x']:>6} {cell(r['per_avg'], '11.0f'):>11} {cell(r['per_max'], '11.0f'):>11} "
              f"{r['t2_n']:>8} {cell(late, '9d'):>9} {cell(c[1] if c else None, '7d'):>7} {t2v:>7} "
              f"{len(s['lat']):>8} {cell(max(s['lat']) if s['lat'] else None, '11d'):>11} "
              f"{cell(max(r['lat']) if r['lat'] else None, '12.0f'):>12} {e1v:>7}")
    print("")
    print("BREAKING POINTS (control and named windows left out, rows in order of growing X)")
    body = sorted((r for r in rows if r["tag"] is None), key=lambda r: r["x"])

    def first(test):
        for i, r in enumerate(body):
            if test(r):
                return i
        return None

    def say(label, i, show):
        if i is None:
            print(f"  {label}: not reached in this sweep")
        else:
            print(f"  {label}: first at X = {body[i]['x']} us ({show(body[i])})"
                  + (f", last good X = {body[i - 1]['x']} us ({show(body[i - 1])})" if i > 0 else ""))

    pshow = lambda r: f"P avg {cell(r['p_avg'], '.1f')} us, max {cell(r['p_max'], '.1f')} us"
    if have:
        if ring:
            say("bytes dropped by the ring", first(lambda r: r["s"]["rx"]["dropped"] > 0), pshow)
        else:
            say("console bytes lost", first(lambda r: r["s"]["count"] and sent - r["s"]["count"][2] > 0), pshow)
    else:
        print("  console bytes lost: no console traffic in this sweep")
    pshow = lambda r: f"period max {cell(r['per_max'], '.0f')} us"
    say("E1 reaction above 10 ms", first(lambda r: r["s"]["lat"] and max(r["s"]["lat"]) > E1_DEADLINE_US), pshow)
    say("T2 deadline missed", first(lambda r: r["s"]["count"] and r["s"]["stats"].get("late") and
                                    (r["s"]["count"][1] > 0 or r["s"]["stats"]["late"]["max"] +
                                     r["s"]["stats"].get("track", {"max": 0})["max"] > T2_DEADLINE_US)), pshow)


if __name__ == "__main__":
    main()
