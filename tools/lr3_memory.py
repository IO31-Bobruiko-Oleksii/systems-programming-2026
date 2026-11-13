#!/usr/bin/env python3
"""LR3 memory analysis of one firmware build.

usage: lr3_memory.py <build-dir> <linker-script> [--measured-static N] [--measured-stack N]
                     [--irq NAME=PRIO ...]

<build-dir> holds the objects (*.o), the map file, firmware.elf and the *.su files of a build made
with -fstack-usage. Needs arm-none-eabi-gcc/size/nm/objdump on the PATH (the Docker image has them).

What it prints:
  1  the memory regions as the linker reports them (--print-memory-usage)
  2  the sections (size -A)
  3  who uses the memory: the groups of object files (from the map file), the biggest symbols
  4  the RAM numbers side by side, so that `size` (bss), the linker and `STAT mem static` agree
  5  the stack: the frame of every function from the code, the deepest call chain, the interrupts,
     the worst case, and the number measured by painting
  6  the heap: whether malloc/_sbrk can be reached
"""
import argparse
import bisect
import collections
import glob
import os
import re
import subprocess
import sys

P = "arm-none-eabi-"
EXC_FRAME = 32


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("lr3_memory: %s failed:\n%s" % (" ".join(cmd[:3]), r.stderr))
    return r.stdout + r.stderr


def title(text):
    print()
    print("== " + text)


def regions(build, ld):
    objs = sorted(glob.glob(os.path.join(build, "*.o")))
    out = os.path.join(build, "pmu.elf")
    text = run([P + "gcc"] + objs + ["-mcpu=cortex-m0plus", "-mthumb", "-specs=nano.specs", "-T" + ld,
                                     "-lc", "-lm", "-lnosys", "-Wl,--gc-sections",
                                     "-Wl,--print-memory-usage", "-o", out])
    title("1. Memory regions (linker, --print-memory-usage)")
    for line in text.splitlines():
        if "Memory region" in line or re.match(r"^\s+(FLASH|RAM):", line):
            print(line.rstrip())


def sections(elf):
    title("2. Sections (size -A)")
    secs = {}
    for line in run([P + "size", "-A", elf]).splitlines():
        m = re.match(r"^(\.\S+)\s+(\d+)\s+(\d+)$", line)
        if m and not m.group(1).startswith((".debug", ".comment", ".ARM.attributes")):
            secs[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    for name, (size, addr) in secs.items():
        where = "FLASH" if addr < 0x20000000 else "RAM"
        print("  %-18s %6d B  at 0x%08x  %s" % (name, size, addr, where))
    return secs


def symbols(elf):
    syms = []
    for line in run([P + "nm", "-S", "--radix=d", "--size-sort", elf]).splitlines():
        f = line.split()
        if len(f) == 4:
            syms.append((int(f[1]), f[2], f[3]))
    return syms


def group_of(path, ours):
    m = re.search(r"([^/\s]+\.a)\(([^)]+)\)", path)
    if m:
        return m.group(1), m.group(0).split("/")[-1]
    base = os.path.basename(path.strip())
    if base in ours:
        return "our code", base
    if base.startswith(("stm32c0xx_hal", "system_stm32", "startup_")):
        return "HAL and startup", base
    return "other", base


def map_usage(mapfile, ours):
    text = open(mapfile, errors="replace").read()
    text = text[text.index("Linker script and memory map"):]
    full = re.compile(r"^ (\.\S+|COMMON)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")
    name_only = re.compile(r"^ (\.\S+|COMMON)$")
    cont = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")
    rows = []
    pending = None
    for line in text.splitlines():
        m = full.match(line)
        if m:
            rows.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)))
            pending = None
            continue
        m = name_only.match(line)
        if m:
            pending = m.group(1)
            continue
        m = cont.match(line)
        if m and pending:
            rows.append((pending, int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
        pending = None
    flash = collections.Counter()
    ram = collections.Counter()
    plain_rodata = 0
    for name, addr, size, path in rows:
        if size == 0 or "(size before relaxing)" in path or path.startswith("*"):
            continue
        if re.search(r"\.str\d+\.\d+$", name):
            continue
        if name.startswith(".rodata") and 0x08000000 <= addr < 0x20000000:
            plain_rodata += size
        group, member = group_of(path, ours)
        if addr >= 0x20000000:
            if name.startswith((".data", ".bss", "COMMON")):
                ram[(group, member)] += size
            if name.startswith(".data"):
                flash[(group, member)] += size
        elif addr >= 0x08000000 and name.startswith((".text", ".rodata", ".isr_vector", ".init", ".fini", ".ARM")):
            flash[(group, member)] += size
    return flash, ram, plain_rodata


def usage(elf, build, ours, secs):
    title("3. Who uses the memory (map file)")
    flash, ram, plain = map_usage(os.path.join(build, "firmware.map"), ours)
    pool = secs.get(".rodata", (0, 0))[0] - plain
    flash[("string constants", "all files, merged")] = pool
    groups = collections.OrderedDict()
    for key in sorted({k[0] for k in list(flash) + list(ram)}):
        groups[key] = (sum(v for k, v in flash.items() if k[0] == key),
                       sum(v for k, v in ram.items() if k[0] == key))
    print("  %-18s %8s %8s" % ("group", "flash B", "RAM B"))
    for key, (f, r) in sorted(groups.items(), key=lambda kv: -kv[1][0]):
        print("  %-18s %8d %8d" % (key, f, r))
    print("  %-18s %8d %8d" % ("total", sum(f for f, _ in groups.values()), sum(r for _, r in groups.values())))
    linker_flash = sum(v[0] for n, v in secs.items() if v[1] < 0x20000000) + secs.get(".data", (0, 0))[0]
    print("  the linker's flash figure (sections + initial .data): %d B; the table is %d B off (alignment)"
          % (linker_flash, sum(f for f, _ in groups.values()) - linker_flash))
    print("  (flash includes the initial values of .data; RAM does not include the heap and stack reserve;")
    print("   the string constants of all files are merged by the linker, so they are one line)")
    print()
    print("  our files, by flash:")
    mine = sorted(((k[1], flash.get(k, 0), ram.get(k, 0)) for k in set(flash) | set(ram) if k[0] == "our code"),
                  key=lambda t: -t[1])
    for member, f, r in mine:
        print("    %-22s %6d flash %6d RAM" % (member, f, r))
    print("  the biggest library members, by flash:")
    libs = sorted(((k[1], v) for k, v in flash.items() if k[0].endswith(".a")), key=lambda t: -t[1])[:6]
    for member, v in libs:
        print("    %-44s %6d" % (member, v))
    syms = symbols(elf)
    print("  the biggest symbols in RAM (.data, .bss):")
    for size, kind, name in [s for s in syms if s[1] in "bBdD"][-8:][::-1]:
        print("    %-24s %6d B" % (name, size))


def ram_numbers(elf, secs, ld, measured_static):
    title("4. RAM numbers side by side")
    sym = {}
    for line in run([P + "nm", "--radix=d", elf]).splitlines():
        f = line.split()
        if len(f) == 3:
            sym[f[2]] = int(f[0])
    m = re.search(r"RAM\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*\S+\s*,\s*LENGTH\s*=\s*(\d+)K", open(ld).read())
    ram = int(m.group(1)) * 1024 if m else sym["_estack"] - 0x20000000
    data = secs.get(".data", (0, 0))[0]
    bss = secs.get(".bss", (0, 0))[0]
    reserve = secs.get("._user_heap_stack", (0, 0))[0]
    static = sym["_end"] - sym["_sdata"]
    rows = [
        (".data", data, "initialised variables"),
        (".bss", bss, "zeroed variables"),
        ("static = _end - _sdata", static, "what `STAT mem static` prints (.data + .bss)"),
        ("._user_heap_stack", reserve, "heap %d + stack %d reserved by the linker script"
         % (sym.get("_Min_Heap_Size", 0), sym.get("_Min_Stack_Size", 0))),
        ("`size` column bss", bss + reserve, ".bss + the reserve: this is why `size` shows more than `static`"),
        ("`size` column data", data, ""),
        ("linker RAM used", static + reserve, "static + reserve (the number in section 1)"),
        ("RAM total", ram, ""),
        ("free after static and reserve", ram - static - reserve, "never used by this build"),
    ]
    for name, value, note in rows:
        print("  %-30s %6d B  %s" % (name, value, note))
    if measured_static is not None:
        verdict = "equal: the target runs this build" if measured_static == static else \
            "DIFFERENT: the build measured in the simulator is not this one"
        print("  `STAT mem static` in the simulator: %d B, %s" % (measured_static, verdict))
    return sym


BRANCH = re.compile(r"^b(?:eq|ne|cs|cc|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?(?:\.[nw])?$")
STOP = re.compile(r"^(bl|blx|bx|pop|cbz|cbnz)$")


class Func:
    def __init__(self, addr, name):
        self.addr, self.name = addr, name
        self.frame = 0
        self.calls = set()
        self.indirect = 0
        self.end = addr
        self.insns = []


def disassemble(elf):
    funcs = {}
    cur = None
    head = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
    insn = re.compile(r"^\s+([0-9a-f]+):\s+([a-z][a-z0-9.]*)\s*(.*)$")
    for line in run([P + "objdump", "-d", "--no-show-raw-insn", elf]).splitlines():
        m = head.match(line)
        if m:
            cur = Func(int(m.group(1), 16), m.group(2))
            funcs[cur.addr] = cur
            continue
        m = insn.match(line)
        if m and cur is not None and not m.group(2).startswith("."):
            cur.insns.append((int(m.group(1), 16), m.group(2), m.group(3)))
            cur.end = int(m.group(1), 16) + 2
    return funcs


def analyse_code(funcs):
    starts = sorted(funcs)
    owner = lambda a: funcs[starts[bisect.bisect_right(starts, a) - 1]]
    for f in funcs.values():
        prologue = True
        regs = 0
        sub = 0
        popped = None
        for addr, mn, ops in f.insns:
            if prologue:
                if mn == "push":
                    regs += len([r for r in ops.split("@")[0].strip("{} \t").split(",") if r.strip()])
                elif mn in ("sub", "subs") and re.match(r"sp,\s*(?:sp,\s*)?#\d+", ops):
                    sub += int(re.search(r"#(\d+)", ops).group(1))
                elif mn == "add" and re.match(r"sp,\s*(?:sp,\s*)?#-\d+", ops):
                    sub += int(re.search(r"#-(\d+)", ops).group(1))
                elif BRANCH.match(mn) or STOP.match(mn):
                    prologue = False
            m = re.match(r"([0-9a-f]+) <", ops)
            if mn == "bl" and m:
                f.calls.add(owner(int(m.group(1), 16)).addr)
            elif mn == "blx":
                f.indirect += 1
            elif BRANCH.match(mn) and m:
                target = int(m.group(1), 16)
                if not (f.addr <= target < f.end):
                    f.calls.add(owner(target).addr)         # a tail call is counted like a call
            elif mn == "bx" and ops.split()[0] not in ("lr", popped):
                f.indirect += 1
            popped = ops.strip("{} \t") if mn == "pop" and "," not in ops else None
        f.frame = regs * 4 + sub


def read_su(build):
    su = {}
    for path in glob.glob(os.path.join(build, "*.su")):
        for line in open(path):
            f = line.rstrip("\n").split("\t")
            if len(f) == 3:
                su[f[0].split(":")[-1]] = (int(f[1]), f[2], os.path.basename(f[0].split(":")[0]))
    return su


def depth(funcs, root, memo, onpath, skip=frozenset()):
    if root in memo:
        return memo[root]
    if root in onpath:
        return (0, [], True)
    onpath.add(root)
    f = funcs[root]
    best = (0, [])
    cyc = False
    for c in f.calls:
        if c in skip:
            continue
        d, chain, cy = depth(funcs, c, memo, onpath, skip)
        cyc = cyc or cy
        if d > best[0]:
            best = (d, chain)
    onpath.discard(root)
    memo[root] = (f.frame + best[0], [root] + best[1], cyc)
    return memo[root]


def vector_handlers(elf, funcs, sym_addr):
    out = run([P + "objdump", "-s", "-j", ".isr_vector", elf])
    words = []
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 2 and re.match(r"^[0-9a-f]+$", f[0]) and not line.startswith("Contents"):
            for w in f[1:5]:
                if re.match(r"^[0-9a-f]{8}$", w):
                    words.append(int.from_bytes(bytes.fromhex(w), "little"))
    names = collections.defaultdict(list)
    for line in run([P + "nm", elf]).splitlines():
        f = line.split()
        if len(f) == 3:
            names[int(f[0], 16)].append(f[2])
    default = next((a for a, ns in names.items() if "Default_Handler" in ns), None)
    reset = next((a for a, ns in names.items() if "Reset_Handler" in ns), None)
    found = collections.OrderedDict()
    for w in words[1:]:
        a = w & ~1
        if a and a != default and a != reset and a in funcs:
            found[a] = sorted(names.get(a, [funcs[a].name]))
    return found


def stack(build, elf, funcs, irq_prio, measured):
    title("5. Stack")
    analyse_code(funcs)
    su = read_su(build)
    byname = {f.name: f for f in funcs.values()}

    print("  largest frames of our code (-fstack-usage):")
    for name, (size, qual, src) in sorted(su.items(), key=lambda kv: -kv[1][0])[:8]:
        print("    %-26s %4d B  %-8s %s" % (name, size, qual, src))
    dynamic = [n for n, v in su.items() if v[1] != "static"]
    print("  functions with a frame that is not 'static': %s" % (", ".join(dynamic) if dynamic else "none"))

    same = diff = 0
    bad = []
    for name, (size, qual, src) in su.items():
        f = byname.get(name)
        if f is None:
            continue
        if f.frame == size:
            same += 1
        else:
            diff += 1
            bad.append("%s %d/%d" % (name, f.frame, size))
    print("  frame read from the machine code == frame from the compiler: %d of %d functions"
          % (same, same + diff) + ("; differ (code/compiler): %s" % ", ".join(bad[:6]) if bad else ""))
    print("  (the same reading is used for the library functions, which have no .su file)")

    memo = {}
    main = byname["main"]
    d, chain, cyc = depth(funcs, main.addr, memo, set())
    alloc = frozenset(byname[n].addr for n in ("_realloc_r", "_malloc_r", "_free_r", "malloc", "free", "realloc")
                      if n in byname)
    d2, chain2, _ = depth(funcs, main.addr, {}, set(), alloc)
    print()
    print("  deepest call chain from main, every direct call followed: %d B" % d)
    for a in chain:
        f = funcs[a]
        mark = "  (calls through a pointer: %d)" % f.indirect if f.indirect else ""
        print("    %-34s %4d B%s" % (f.name, f.frame, mark))
    if cyc:
        print("  recursion found: the chain above is cut where it repeats")
    print("  the same without malloc/realloc/free: %d B (the chain ends in %s)"
          % (d2, funcs[chain2[-1]].name))
    print("  (vsnprintf writes into a fixed buffer, so its helper never reallocates: see section 6)")

    handlers = vector_handlers(elf, funcs, {f.name: f.addr for f in funcs.values()})
    levels = collections.defaultdict(lambda: (0, ""))
    print()
    print("  interrupt handlers in the vector table (exception frame %d B + the handler):" % EXC_FRAME)
    skipped = ("NMI", "HardFault", "SVC", "PendSV")
    for a, names in handlers.items():
        dh, ch, _ = depth(funcs, a, memo, set())
        prio = None
        for key, p in irq_prio.items():
            if any(key in n for n in names):
                prio = p
        label = "/".join(names)
        if any(s in label for s in skipped):
            print("    %-34s depth %4d B  (not used: a fault ends in a loop)" % (label, dh))
            continue
        print("    %-34s depth %4d B  priority %s  deepest call: %s"
              % (label, dh, "?" if prio is None else prio, funcs[ch[-1]].name))
        key = prio if prio is not None else "?" + label
        if dh > levels[key][0]:
            levels[key] = (dh, label)
    nested = sum(EXC_FRAME + v[0] for v in levels.values())
    total = d + nested
    total2 = d2 + nested
    print()
    print("  nested interrupts: %d priority levels, %s = %d B"
          % (len(levels), " + ".join("%d+%d" % (EXC_FRAME, v[0]) for v in levels.values()), nested))
    print("  worst case, every direct call followed:  %d + %d = %d B, margin to the 1024 B reserve %d B"
          % (d, nested, total, 1024 - total))
    print("  worst case without the allocator calls:  %d + %d = %d B, margin to the 1024 B reserve %d B"
          % (d2, nested, total2, 1024 - total2))
    if measured is not None:
        print("  measured by painting (STAT mem stack_used, largest of the runs): %d B" % measured)
        print("  without the allocator: static / measured = %.2f, static is %d B more"
              % (total2 / measured, total2 - measured))
    return memo, byname, handlers


def heap(funcs, byname, handlers):
    title("6. Heap")
    names = ("malloc", "_malloc_r", "_sbrk", "_sbrk_r", "calloc", "realloc", "free")
    present = [n for n in names if n in byname]
    print("  heap functions in the image: %s" % (", ".join(present) if present else "none"))
    roots = [byname["main"].addr] + list(handlers)
    parent = {r: None for r in roots}
    queue = list(roots)
    while queue:
        a = queue.pop(0)
        for c in funcs[a].calls:
            if c not in parent:
                parent[c] = a
                queue.append(c)
    for n in ("_sbrk", "malloc", "_malloc_r"):
        if n not in byname:
            continue
        a = byname[n].addr
        if a in parent:
            path = []
            while a is not None:
                path.append(funcs[a].name)
                a = parent[a]
            print("  %s is reachable: %s" % (n, " <- ".join(path)))
        else:
            print("  %s is not reachable by direct calls from main or an interrupt" % n)
    print("  newlib-nano calls the allocator from vsnprintf only to grow a buffer it allocated itself")
    print("  (asprintf); say() gives it a fixed 160-byte buffer, so the call is never made. The")
    print("  measured `heap_used` (STAT mem) must be 0 in every run: that is the proof.")
    print("  (calls through pointers are not followed: %d functions have some)"
          % sum(1 for f in funcs.values() if f.indirect))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("ld")
    ap.add_argument("--measured-static", type=int)
    ap.add_argument("--measured-stack", type=int)
    ap.add_argument("--irq", action="append", default=[], help="NAME=PRIORITY")
    args = ap.parse_args()
    elf = os.path.join(args.build, "firmware.elf")
    irq = {kv.split("=")[0]: int(kv.split("=")[1]) for kv in args.irq}
    ours = set(os.path.basename(p)[:-2] + ".o" for d in ("platform/src", "devices/src", "app/src")
               for p in glob.glob(os.path.join(os.path.dirname(os.path.abspath(args.ld)), d, "*.c")))
    ours.add("main.o")
    ours.add("stm32c0xx_it.o")
    print("memory analysis of %s" % elf)
    print(run([P + "gcc", "--version"]).splitlines()[0])
    regions(args.build, args.ld)
    secs = sections(elf)
    usage(elf, args.build, ours, secs)
    ram_numbers(elf, secs, args.ld, args.measured_static)
    funcs = disassemble(elf)
    memo, byname, handlers = stack(args.build, elf, funcs, irq, args.measured_stack)
    heap(funcs, byname, handlers)


if __name__ == "__main__":
    main()
