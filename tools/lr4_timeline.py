#!/usr/bin/env python3
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import os

UK = os.environ.get("LR4_LANG", "en") == "uk"
LANES = ([("D3", "кнопка PB5"), ("D2", "EVENT: обробник stow"), ("D0", "зайнятість (PB6)"),
          ("D6", "вибір SD-картки (CS)"), ("D4", "UART TX"), ("D5", "ШІМ сервопривода 1")] if UK else
         [("D3", "button PB5"), ("D2", "EVENT: stow handler"), ("D0", "busy (PB6)"),
          ("D6", "SD chip select"), ("D4", "UART TX"), ("D5", "servo 1 PWM")])


def read_vcd(path):
    ids = {}
    tr = {}
    t = 0
    with open(path) as f:
        for ln in f:
            ln = ln.strip()
            m = re.match(r"\$var wire 1 (\S+) (\S+) \$end", ln)
            if m:
                ids[m.group(1)] = m.group(2)
                tr[m.group(2)] = []
                continue
            if ln.startswith("#"):
                t = int(ln[1:])
            elif ln and ln[0] in "01" and ln[1:] in ids:
                tr[ids[ln[1:]]].append((t / 1e6, int(ln[0])))
    return tr


def first_fall(tr):
    prev = None
    for t, v in tr["D3"]:
        if prev == 1 and v == 0:
            return t
        prev = v
    raise SystemExit("no falling edge on D3")


def pulses(tr, ch, level=1):
    out = []
    start = None
    cur = None
    for t, v in tr[ch]:
        if v == level and cur != level:
            start = t
        if v != level and cur == level and start is not None:
            out.append((start, t))
            start = None
        cur = v
    return out


def draw(ax, tr, t0, lo, hi, title):
    n = len(LANES)
    for i, (ch, label) in enumerate(LANES):
        y0 = n - 1 - i
        segs = []
        cur = 0
        last_t = lo
        val = 0
        for t, v in tr[ch]:
            if t - t0 <= lo:
                val = v
        pts_t = [lo]
        pts_v = [val]
        for t, v in tr[ch]:
            x = t - t0
            if x < lo or x > hi:
                continue
            pts_t += [x, x]
            pts_v += [pts_v[-1], v]
        pts_t.append(hi)
        pts_v.append(pts_v[-1])
        ax.step(pts_t, [y0 + 0.7 * v for v in pts_v], where="post", color="#1f4e79", lw=0.9)
        ax.text(lo - 0.3, y0 + 0.35, label, ha="right", va="center", fontsize=float(os.environ.get("LR4_FONT", "8")))
    ax.axvline(0, color="#c00000", lw=0.8, ls="--")
    ax.set_xlim(lo, hi)
    ax.set_ylim(-0.2, n)
    ax.set_yticks([])
    ax.set_title(title, fontsize=float(os.environ.get("LR4_FONT", "8")) + 1, loc="left")
    ax.grid(axis="x", color="#dddddd", lw=0.5)


def report(name, tr, t0):
    ev = [(s - t0, e - t0) for s, e in pulses(tr, "D2")]
    cs = [(s - t0, e - t0) for s, e in pulses(tr, "D6", 0)]
    print("%s: press (first falling edge) at %.3f ms of the capture" % (name, t0))
    for s, e in ev:
        print("  EVENT pulse %.3f .. %.3f ms after the edge (%.1f us)" % (s, e, (e - s) * 1000))
    for s, e in cs:
        if -5 < s < 60:
            print("  SD chip select low %.3f .. %.3f ms after the edge (%.2f ms)" % (s, e, e - s))


def main():
    loop, rtos, out = sys.argv[1:4]
    lo = float(sys.argv[4]) if len(sys.argv) > 4 else -3.0
    hi = float(sys.argv[5]) if len(sys.argv) > 5 else 40.0
    tl, tr_ = read_vcd(loop), read_vcd(rtos)
    t_l, t_r = first_fall(tl), first_fall(tr_)
    report("loop", tl, t_l)
    report("rtos", tr_, t_r)
    FS = tuple(float(v) for v in os.environ.get("LR4_FIG", "10,7").split(","))
    fig, axs = plt.subplots(2, 1, figsize=FS, sharex=True)
    if UK:
        t0 = "Суперцикл (ЛР3): натискання чекає, поки закінчаться запис на SD і відповідь на команду"
        t1 = "FreeRTOS (ЛР4): задача event витісняє команду через 0,14 мс; запис на SD починається пізніше"
        xl = "мс після першого спадного фронту кнопки (штрихова лінія)"
    else:
        t0 = "Super loop (LR3): the press waits until the SD write and its reply end"
        t1 = "FreeRTOS (LR4): the event task preempts the command after about 0.14 ms"
        xl = "ms after the first falling edge of the button (dashed line)"
    draw(axs[0], tl, t_l, lo, hi, t0)
    draw(axs[1], tr_, t_r, lo, hi, t1)
    axs[1].set_xlabel(xl)
    fig.tight_layout()
    fig.savefig(out, dpi=int(os.environ.get("LR4_DPI", "150")), bbox_inches="tight")


main()