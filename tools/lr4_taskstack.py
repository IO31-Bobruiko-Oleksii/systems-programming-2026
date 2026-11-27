#!/usr/bin/env python3
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lr3_memory as m
build = sys.argv[1]
funcs = m.disassemble(os.path.join(build, "firmware.elf"))
m.analyse_code(funcs)
byname = {f.name: f for f in funcs.values()}
skip = frozenset(byname[n].addr for n in ("rtos_assert","fatal_text","vApplicationStackOverflowHook","vApplicationMallocFailedHook") if n in byname)
generic = ["hook_enter","hook_leave","hook_lock","hook_unlock","hook_sd_lock","hook_sd_unlock",
           "hook_log_put","hook_log_drain","hook_load_start","hook_stats_reset","hook_busy_cycles"]
outh = ["hook_out_lock","hook_out_unlock"]
def addrs(names): return [byname[n].addr for n in names if n in byname]
def has_blx(f):
    return any(mn == "blx" for _, mn, _ in f.insns)
for f in funcs.values():
    if f.name.startswith("hook_"): continue
    if has_blx(f):
        if f.name == "say": f.calls.update(addrs(outh))
        elif f.name == "app_execute": f.calls.update(addrs(generic + outh + ["hook_command"]))
        else: f.calls.update(addrs(generic))
def d(name):
    return m.depth(funcs, byname[name].addr, {}, set(), skip)
for h in generic + outh + ["hook_command"]:
    if h in byname:
        dd, chain, cyc = d(h)
        print("%-18s %4d B cyc=%s" % (h, dd, cyc))
print()
for t in ["event_task","track_task","cmd_task","logger_task","load_task"]:
    dd, chain, cyc = d(t)
    print("%-12s %4d B  cyc=%s\n   %s" % (t, dd, cyc, " > ".join("%s(%d)" % (funcs[a].name, funcs[a].frame) for a in chain)))