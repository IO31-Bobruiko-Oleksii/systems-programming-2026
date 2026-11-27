#!/usr/bin/env python3
import re
import sys

FLASH0, FLASH1 = 0x08000000, 0x08008000
RAM0, RAM1 = 0x20000000, 0x20003000

KERNEL = ("tasks_lr4.o", "tasks.o", "queue.o", "list.o", "port.o", "portasm.o", "heap_1.o")


def group_of(obj):
    base = obj.split("/")[-1]
    if "(" in obj:
        lib = obj.split("(")[0].split("/")[-1]
        if "libgcc" in lib or "libgcc" in obj:
            return "libgcc"
        return "libc/libm"
    if base in KERNEL:
        return "kernel"
    if base == "rtos_app.o":
        return "rtos layer"
    if base in ("app.o",):
        return "app"
    if base in ("logger.o", "perf.o", "pots.o", "sdcard.o", "servo.o", "tracker.o", "fmt.o"):
        return "devices"
    if base.startswith("plat_"):
        return "platform"
    if base.startswith("stm32c0xx_hal") and base not in ("stm32c0xx_hal_msp.o",):
        return "HAL"
    if base.startswith("libgcc") or "libgcc" in obj:
        return "libgcc"
    return "Core, startup"


def parse(path):
    text = open(path, errors="replace").read()
    i = text.find("Linker script and memory map")
    lines = text[i:].splitlines()
    rows = []
    pending = None
    one = re.compile(r"^\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S.*)$")
    full = re.compile(r"^ (\.\S+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S.*)$")
    name_only = re.compile(r"^ (\.\S+)\s*$")
    for ln in lines:
        m = full.match(ln)
        if m:
            rows.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4).strip()))
            pending = None
            continue
        m = name_only.match(ln)
        if m:
            pending = m.group(1)
            continue
        m = one.match(ln)
        if m and pending:
            rows.append((pending, int(m.group(1), 16), int(m.group(2), 16), m.group(3).strip()))
            pending = None
            continue
        pending = None
    out = {}
    for sec, vma, size, obj in rows:
        if size == 0 or not (obj.endswith(".o") or ".o)" in obj):
            continue
        if re.search(r"\.(str|cst)\d", sec):
            continue
        flash = ram = 0
        if FLASH0 <= vma < FLASH1:
            flash = size
        elif RAM0 <= vma < RAM1:
            ram = size
            if sec.startswith(".data"):
                flash = size
        else:
            continue
        key = obj
        f, r = out.get(key, (0, 0))
        out[key] = (f + flash, r + ram)
    return out


def flash_total(path):
    text = open(path, errors="replace").read()
    m = re.search(r"^\.data\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+load address 0x([0-9a-fA-F]+)",
                  text, re.M)
    if not m:
        return None
    return int(m.group(3), 16) + int(m.group(2), 16) - FLASH0


def main(paths):
    for path in paths:
        objs = parse(path)
        groups = {}
        for obj, (f, r) in objs.items():
            g = group_of(obj)
            gf, gr = groups.get(g, (0, 0))
            groups[g] = (gf + f, gr + r)
        print("map %s" % path)
        print("  %-16s %8s %8s" % ("group", "flash B", "RAM B"))
        tf = tr = 0
        for g in ("Core, startup", "HAL", "platform", "devices", "app", "rtos layer", "kernel",
                  "libc/libm", "libgcc"):
            f, r = groups.get(g, (0, 0))
            tf += f
            tr += r
            print("  %-16s %8d %8d" % (g, f, r))
        ft = flash_total(path)
        if ft is not None:
            print("  %-16s %8d %8d" % ("string pool", ft - tf, 0))
            tf = ft
        print("  %-16s %8d %8d" % ("total", tf, tr))
        ker = [(o.split("/")[-1], v) for o, v in objs.items()
               if o.split("/")[-1] in KERNEL and "(" not in o]
        if ker:
            print("  kernel objects:")
            for o, (f, r) in sorted(ker):
                print("    %-14s %8d %8d" % (o, f, r))
        top = sorted(((o.split("/")[-1], v) for o, v in objs.items() if "(" not in o),
                     key=lambda x: -x[1][0])[:8]
        print("  largest objects by flash: " + ", ".join("%s %d" % (o, v[0]) for o, v in top))
        print()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])