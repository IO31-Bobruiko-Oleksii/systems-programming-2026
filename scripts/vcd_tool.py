#!/usr/bin/env python3
import argparse
import bisect
import hashlib
import os
import re
import shutil
import statistics
import subprocess
import sys

BAUD = 115200
BIT_NS = 1e9 / BAUD
SD_NAMES = {
    0: "GO_IDLE_STATE", 8: "SEND_IF_COND", 17: "READ_SINGLE_BLOCK", 41: "SD_SEND_OP_COND",
    55: "APP_CMD", 58: "READ_OCR",
}
R1_BITS = ["idle", "erase-reset", "illegal-command", "crc-error", "erase-sequence-error",
           "address-error", "parameter-error"]
FAST_SCK_NS = 1000


class Capture:
    def __init__(self, path):
        self.path = path
        self.names = {}
        self.ch = {}
        self.last = 0
        with open(path, "rb") as f:
            self.sha256 = hashlib.sha256(f.read()).hexdigest()
        t = 0
        in_defs = True
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                if in_defs:
                    m = re.match(r"\$var\s+\w+\s+\d+\s+(\S+)\s+(\S+)\s+\$end", line)
                    if m:
                        self.names[m.group(1)] = m.group(2)
                        self.ch[m.group(2)] = []
                    elif line.startswith("$enddefinitions"):
                        in_defs = False
                    continue
                if line[0] == "#":
                    t = int(line[1:])
                    self.last = t
                elif line[0] in "01" and line[1:] in self.names:
                    self.ch[self.names[line[1:]]].append((t, int(line[0])))
        for c in ("D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7"):
            if c not in self.ch:
                raise SystemExit(f"{path}: channel {c} is missing (is the logic analyzer in diagram.json?)")
        self._times = {c: [x[0] for x in v] for c, v in self.ch.items()}

    def value_at(self, c, t):
        i = bisect.bisect_right(self._times[c], t) - 1
        return self.ch[c][i][1] if i >= 0 else None

    def initial(self, c):
        return self.ch[c][0][1]

    def edges(self, c, val=None):
        out = []
        prev = None
        for t, v in self.ch[c]:
            if prev is None:
                prev = v
                if t == 0:
                    continue
            if v != prev and (val is None or v == val):
                out.append(t)
            prev = v
        return out

    def pulses(self, c):
        """(t_rise, width) of every complete high pulse."""
        out = []
        tr = None
        prev = None
        for t, v in self.ch[c]:
            if prev is None:
                prev = v
                if v == 1:
                    tr = t
                continue
            if v != prev:
                if v == 1:
                    tr = t
                elif tr is not None:
                    out.append((tr, t - tr))
                    tr = None
            prev = v
        return out



def uart_frames(cap, chan="D4"):
    """8N1 frames as (t_start, byte, stop_ok). Decoding begins at the first rising edge (line idle)."""
    rises = cap.edges(chan, 1)
    if not rises:
        return []
    out = []
    t_ok = 0
    for tf in cap.edges(chan, 0):
        if tf < rises[0] or tf < t_ok or cap.value_at(chan, tf + BIT_NS / 2) != 0:
            continue
        b = 0
        for k in range(8):
            if cap.value_at(chan, tf + BIT_NS * (1.5 + k)):
                b |= 1 << k
        out.append((tf, b, cap.value_at(chan, tf + BIT_NS * 9.5) == 1))
        t_ok = tf + BIT_NS * 9.5
    return out


def uart_lines(frames, bit_ns):
    """Split into lines at LF: (t_first_start, t_end_of_last_stop_bit, text, ends_with_crlf)."""
    out = []
    cur = []
    t0 = None
    prev = None
    for t, b, _ in frames:
        if t0 is None:
            t0 = t
        if b == 10:
            text = bytes(x for x in cur if x not in (10, 13)).decode("ascii", "replace")
            out.append((t0, t + 10 * bit_ns, text, prev == 13))
            cur = []
            t0 = None
        else:
            cur.append(b)
        prev = b
    return out


def fit_bit_time(cap, frames, chan="D4"):
    edges = cap.edges(chan)
    num = den = 0.0
    for t0, _, _ in frames:
        i = bisect.bisect_right(edges, t0)
        while i < len(edges) and edges[i] <= t0 + 9.6 * BIT_NS:
            k = round((edges[i] - t0) / BIT_NS)
            num += k * (edges[i] - t0)
            den += k * k
            i += 1
    return num / den if den else BIT_NS



def spi_bytes(cap):
    """All SPI bytes in time order: dicts with t, mosi, miso, cs (1 = asserted/low), fast."""
    cs_edges = sorted(cap.edges("D3"))
    rises = cap.edges("D0", 1)
    seg = {}
    for t in rises:
        seg.setdefault(bisect.bisect_right(cs_edges, t), []).append(t)
    out = []
    for k in sorted(seg):
        ts = seg[k]
        if len(ts) % 8:
            raise SystemExit(f"SPI segment {k}: {len(ts)} clock edges, not a multiple of 8")
        for i in range(0, len(ts), 8):
            chunk = ts[i:i + 8]
            mo = mi = 0
            for t in chunk:
                mo = (mo << 1) | cap.value_at("D1", t)
                mi = (mi << 1) | cap.value_at("D2", t)
            out.append({"t": chunk[0], "t_end": chunk[-1], "mosi": mo, "miso": mi,
                        "cs": 1 if cap.value_at("D3", chunk[0]) == 0 else 0,
                        "fast": 1 if (chunk[-1] - chunk[0]) / 7 < FAST_SCK_NS else 0,
                        "period": (chunk[-1] - chunk[0]) / 7})
    return out


def crc16_xmodem(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def r1_text(r1):
    flags = [n for i, n in enumerate(R1_BITS) if r1 & (1 << i)]
    return "ready" if not flags and r1 == 0 else ",".join(flags) if flags else f"0x{r1:02X}"


def annotate_sd(win):
    """win: list of byte dicts inside one CS-low window. Returns text lines."""
    mo = [b["mosi"] for b in win]
    mi = [b["miso"] for b in win]
    lines = []
    i = 0
    prev_cmd = None
    while i < len(win):
        if mo[i] != 0xFF and (mo[i] & 0xC0) == 0x40 and i + 6 <= len(win):
            cmd = mo[i] & 0x3F
            arg = int.from_bytes(bytes(mo[i + 1:i + 5]), "big")
            crc = mo[i + 5]
            name = SD_NAMES.get(cmd, "?")
            label = f"CMD{cmd}" if prev_cmd != 55 else f"ACMD{cmd}"
            j = i + 6
            r1 = None
            while j < len(win) and j < i + 6 + 8:
                if mi[j] & 0x80 == 0:
                    r1 = mi[j]
                    break
                j += 1
            head = f"{win[i]['t'] / 1e6:9.4f} ms  {label:<6} {name:<18} arg={arg:08X} crc={crc:02X}"
            if r1 is None:
                lines.append(head + "  -> no R1 within 8 bytes")
                i += 6
                prev_cmd = cmd
                continue
            idle = j - (i + 6)
            lines.append(head + f"  -> R1={r1:02X} ({r1_text(r1)}), {idle} idle byte(s) before it")
            nxt = j + 1
            if cmd == 8 and r1 <= 1 and nxt + 4 <= len(win):
                r7 = mi[nxt:nxt + 4]
                lines.append(f"{'':14}R7={''.join(f'{x:02X}' for x in r7)}  voltage-accepted={r7[2] & 0xF}  check-pattern={r7[3]:02X}")
                nxt += 4
            elif cmd == 58 and r1 == 0 and nxt + 4 <= len(win):
                ocr = mi[nxt:nxt + 4]
                v = int.from_bytes(bytes(ocr), "big")
                lines.append(f"{'':14}OCR={v:08X}  power-up-done={v >> 31 & 1}  CCS(SDHC)={v >> 30 & 1}")
                nxt += 4
            elif cmd == 17 and r1 == 0:
                k = nxt
                while k < len(win) and mi[k] == 0xFF:
                    k += 1
                if k < len(win) and mi[k] == 0xFE and k + 1 + 512 + 2 <= len(win):
                    blk = bytes(mi[k + 1:k + 513])
                    crc_rx = (mi[k + 513] << 8) | mi[k + 514]
                    crc_c = crc16_xmodem(blk)
                    lines.append(f"{'':14}data token FE at {win[k]['t'] / 1e6:.4f} ms after {k - nxt} idle byte(s); "
                                 f"512 data bytes {win[k + 1]['t'] / 1e6:.4f}..{win[k + 512]['t'] / 1e6:.4f} ms")
                    lines.append(f"{'':14}CRC16 received={crc_rx:04X} computed={crc_c:04X} {'OK' if crc_rx == crc_c else 'MISMATCH'}; "
                                 f"bytes 510..511 = {blk[510]:02X}{blk[511]:02X}")
                    if blk[450] != 0:
                        lba = int.from_bytes(blk[454:458], "little")
                        cnt = int.from_bytes(blk[458:462], "little")
                        lines.append(f"{'':14}MBR partition 1: type {blk[450]:02X}, first LBA {lba}, {cnt} sectors")
                    nxt = k + 513 + 2
                else:
                    lines.append(f"{'':14}no data token in this window")
            i = nxt
            prev_cmd = cmd
        else:
            i += 1
    return lines



def report(cap):
    out = []
    p = out.append
    ts = {t for c in cap.ch.values() for t, _ in c}
    grid = 25 if all(t % 25 == 0 for t in ts) else 1
    p(f"capture {os.path.basename(cap.path)}  sha256 {cap.sha256[:16]}  duration {cap.last / 1e6:.3f} ms  "
      f"timestamps on a {grid} ns grid")
    p("edges per channel: " + "  ".join(f"{c}={len(cap.edges(c))}" for c in sorted(cap.ch)))
    p("")

    # UART
    fr = uart_frames(cap)
    bit = fit_bit_time(cap, fr)
    bad = sum(1 for f in fr if not f[2])
    p(f"UART D4: {len(fr)} bytes, framing errors {bad}, fitted bit time {bit:.1f} ns = {1e9 / bit:.0f} baud "
      f"({(1e9 / bit / BAUD - 1) * 100:+.2f} % vs 115200; BRR 104 at 12 MHz gives 115385 baud)")
    sp = [b[0] - a[0] for a, b in zip(fr, fr[1:]) if b[0] - a[0] < 20 * bit]
    if sp:
        p(f"  frame start to frame start inside a line: median {statistics.median(sp) / 1000:.3f} us "
          f"= {statistics.median(sp) / bit:.2f} bit times (10.00 would be back to back)")
    for t0, t1, text, crlf in uart_lines(fr, bit):
        p(f"  {t0 / 1e6:9.4f} .. {t1 / 1e6:9.4f} ms  {text}{'' if crlf else '   [no CR before LF]'}")
    p("")

    # PWM
    for c, who in (("D5", "servo 1, TIM3_CH1 PC6"), ("D6", "servo 2, TIM3_CH2 PC7")):
        pl = cap.pulses(c)
        p(f"PWM {c} ({who}): {len(pl)} pulses")
        for k, (t, w) in enumerate(pl):
            per = f"{(pl[k + 1][0] - t) / 1000:.3f}" if k + 1 < len(pl) else "-"
            p(f"  rise {t / 1e6:9.4f} ms  width {w / 1000:8.3f} us  period to next {per} us")
    p("")

    # ADC marker
    pl = cap.pulses("D7")
    p(f"ADC marker D7 (PB3): {len(pl)} pulses")
    group = []
    groups = []
    for t, w in pl:
        if group and t - (group[-1][0] + group[-1][1]) > 1_000_000:
            groups.append(group)
            group = []
        group.append((t, w))
    if group:
        groups.append(group)
    for g in groups:
        gaps = [b[0] - (a[0] + a[1]) for a, b in zip(g, g[1:])]
        p(f"  burst at {g[0][0] / 1e6:.4f} ms: {len(g)} conversions, widths "
          f"{', '.join(f'{w / 1000:.3f}' for _, w in g)} us, gaps {', '.join(f'{x / 1000:.3f}' for x in gaps)} us, "
          f"burst length {(g[-1][0] + g[-1][1] - g[0][0]) / 1000:.2f} us")
    p("")

    # SPI
    bs = spi_bytes(cap)
    cs_f = cap.edges("D3", 0)
    cs_r = cap.edges("D3", 1)
    p(f"CS D3 (PB9): initial {cap.initial('D3')}, falling edges {[round(x / 1e6, 4) for x in cs_f]} ms, "
      f"rising edges {[round(x / 1e6, 4) for x in cs_r]} ms")
    p(f"MISO D2 (PA6): {len(cap.edges('D2'))} edges, initial {cap.initial('D2')}")
    p(f"SPI: {len(bs)} bytes, {sum(1 for b in bs if b['cs'])} with CS low")
    idle = [b for b in bs if not b["cs"]]
    if idle:
        times = ", ".join("%.4f" % (b["t"] / 1e6) for b in idle)
        p(f"  CS high: {len(idle)} bytes of clocks (MOSI all FF: {all(b['mosi'] == 0xFF for b in idle)}), "
          f"at {times} ms")
    wins = []
    cur = []
    for b in bs:
        if b["cs"]:
            cur.append(b)
        elif cur:
            wins.append(cur)
            cur = []
    if cur:
        wins.append(cur)
    for n, w in enumerate(wins, 1):
        per = statistics.mean(b["period"] for b in w)
        gaps = [b["t"] - a["t_end"] for a, b in zip(w, w[1:])]
        p(f"  CS low window {n}: {w[0]['t'] / 1e6:.4f} .. {w[-1]['t_end'] / 1e6:.4f} ms, {len(w)} bytes, "
          f"SCK period {per:.1f} ns = {1e6 / per:.1f} kHz, byte-to-byte gap median {statistics.median(gaps) / 1000:.2f} us "
          f"(max {max(gaps) / 1000:.2f} us)")
        for ln in annotate_sd(w):
            p("    " + ln)
        if all(b["miso"] == 0 for b in w):
            p("    note: MISO is 0x00 in every byte of this window, nothing drives the line "
              "(no card); the R1=00 above is not an answer from a card")
        k = min(48, len(w))
        p(f"    MOSI[0:{k}] " + " ".join(f"{b['mosi']:02X}" for b in w[:k]))
        p(f"    MISO[0:{k}] " + " ".join(f"{b['miso']:02X}" for b in w[:k]))
    return "\n".join(out) + "\n"



def vectors(cap, name, out_path):
    bs = spi_bytes(cap)
    n = len(bs)
    lines = [
        "/* GENERATED by scripts/vcd_tool.py vectors; do not edit.",
        f" * Source: {os.path.basename(cap.path)}, sha256 {cap.sha256}",
        f" * {n} SPI bytes in time order, decoded as SPI mode 0 from SCK D0, MOSI D1, MISO D2, CS D3.",
        " * FLAGS bit0 = CS asserted (low) while the byte was clocked, bit1 = fast clock (6 MHz, SCK period < 1 us). */",
        f"#define {name}_LEN {n}U",
    ]

    def arr(label, vals):
        lines.append(f"static const uint8_t {name}_{label}[{name}_LEN] = {{")
        for i in range(0, len(vals), 16):
            lines.append("    " + ", ".join(f"0x{v:02X}" for v in vals[i:i + 16]) + ",")
        lines.append("};")

    arr("MOSI", [b["mosi"] for b in bs])
    arr("MISO", [b["miso"] for b in bs])
    arr("FLAGS", [b["cs"] | (b["fast"] << 1) for b in bs])
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{out_path}: {n} bytes from {cap.path}")



def sigrok_values(args):
    out = subprocess.run(["sigrok-cli"] + args, capture_output=True, text=True).stdout
    vals = []
    for ln in out.splitlines():
        m = re.match(r"^\S+: ([0-9A-F]{2})$", ln.strip())
        if m:
            vals.append(int(m.group(1), 16))
    return vals


def crosscheck(cap):
    if shutil.which("sigrok-cli") is None:
        print("sigrok-cli not installed (sudo apt install sigrok-cli): cross-check skipped")
        return 0
    ok = True
    u_mine = [b for _, b, _ in uart_frames(cap)]
    u_sig = sigrok_values(["-I", "vcd:downsample=250", "-i", cap.path, "-P", "uart:tx=D4:baudrate=115200",
                           "-A", "uart=tx-data"])
    ok &= u_mine == u_sig
    print(f"UART: this tool {len(u_mine)} bytes, sigrok-cli {len(u_sig)} bytes, identical: {u_mine == u_sig}")
    sb = [b for b in spi_bytes(cap) if b["cs"]]
    spi = "spi:clk=D0:mosi=D1:miso=D2:cs=D3"
    mo_sig = sigrok_values(["-I", "vcd:downsample=25", "-i", cap.path, "-P", spi, "-A", "spi=mosi-data"])
    mi_sig = sigrok_values(["-I", "vcd:downsample=25", "-i", cap.path, "-P", spi, "-A", "spi=miso-data"])
    same = [b["mosi"] for b in sb] == mo_sig and [b["miso"] for b in sb] == mi_sig
    ok &= same
    print(f"SPI (CS low): this tool {len(sb)} bytes, sigrok-cli MOSI {len(mo_sig)} / MISO {len(mi_sig)}, identical: {same}")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for c in ("report", "crosscheck"):
        sub.add_parser(c).add_argument("vcd")
    v = sub.add_parser("vectors")
    v.add_argument("vcd")
    v.add_argument("--name", required=True, help="C identifier prefix, e.g. SD_BOOT")
    v.add_argument("--out", required=True)
    a = ap.parse_args()
    cap = Capture(a.vcd)
    if a.cmd == "report":
        sys.stdout.write(report(cap))
    elif a.cmd == "vectors":
        vectors(cap, a.name, a.out)
    else:
        sys.exit(crosscheck(cap))


if __name__ == "__main__":
    main()
