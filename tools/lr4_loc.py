#!/usr/bin/env python3
import re
import sys
import os

MEAS = re.compile(r"^\s*#\s*(ifdef|ifndef|if)\b.*\b(RTOS_BENCH|RTOS_DIAG|RTOS_SCEN|RTOS_INV|RTOS_STARVE|RTOS_QFLOOD)\b")


def count_file(path):
    code = meas = 0
    in_block = False
    stack = []
    for raw in open(path, errors="replace"):
        s = raw.strip()
        if in_block:
            if "*/" in s:
                in_block = False
                s = s.split("*/", 1)[1].strip()
            else:
                continue
        if s.startswith("/*"):
            if "*/" not in s:
                in_block = True
                continue
            s = s.split("*/", 1)[1].strip()
        if not s or s.startswith("//"):
            continue
        if re.match(r"^\s*#\s*if", s):
            stack.append(bool(MEAS.match(s)))
        elif re.match(r"^\s*#\s*endif", s):
            if stack:
                stack.pop()
            continue
        elif re.match(r"^\s*#\s*(else|elif)", s):
            continue
        if any(stack):
            meas += 1
        else:
            code += 1
    return code, meas


def main():
    root = sys.argv[1]
    tot_c = tot_m = 0
    for d in sys.argv[2:]:
        c = m = 0
        files = 0
        for dp, _, fn in os.walk(os.path.join(root, d)):
            for f in fn:
                if f.endswith((".c", ".h")):
                    a, b = count_file(os.path.join(dp, f))
                    c += a
                    m += b
                    files += 1
        print("%-22s files %2d  code %5d  meas %4d" % (d, files, c, m))
        tot_c += c
        tot_m += m
    print("%-22s            code %5d  meas %4d" % ("total", tot_c, tot_m))


main()