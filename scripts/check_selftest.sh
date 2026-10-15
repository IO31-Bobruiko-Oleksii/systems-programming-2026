#!/usr/bin/env bash
set -euo pipefail

log="${1:?usage: $0 <serial-log>}"
[ -r "$log" ] || { echo "check_selftest: cannot read $log" >&2; exit 2; }

tr -d '\r' < "$log" | awk '
function bad(msg) { print "SELFTEST MALFORMED: " msg; failed_format = 1; exit 2 }
function expect_name(n) { return (n == 1) ? "adc" : (n == 2) ? "spi" : (n == 3) ? "sd" : (n == 4) ? "servo1" : "servo2" }

/^SELFTEST BEGIN version=1$/ && !begun { begun = 1; next }
begun && !ended && /^SELFTEST END / {
    ended = 1
    if ($0 !~ /^SELFTEST END total=[0-9]+ passed=[0-9]+ failed=[0-9]+$/) bad("bad END line: " $0)
    split($0, f, /[ =]/)             # f[4]=total f[6]=passed f[8]=failed
    end_total = f[4]; end_passed = f[6]; end_failed = f[8]
    next
}
begun && !ended && /^SELFTEST / {
    n++
    name = $2
    if (n > 5) bad("more than 5 check lines")
    if (name != expect_name(n)) bad("check " n " should be " expect_name(n) " but is " name)
    ok = 0
    if (name == "adc"  && $0 ~ /^SELFTEST adc OK raw=[0-9]+,[0-9]+,[0-9]+,[0-9]+$/) {
        split(substr($0, index($0, "raw=") + 4), v, ",")
        for (i = 1; i <= 4; i++) if (v[i] + 0 > 4095) bad("adc value above 4095: " $0)
        ok = 1; pass++
    } else if (name == "adc"  && $0 ~ /^SELFTEST adc FAIL status=[0-9]+ at=-?[0-9]+$/) { ok = 1; fail++ }
    else if (name == "spi"    && $0 ~ /^SELFTEST spi OK$/)                                 { ok = 1; pass++ }
    else if (name == "spi"    && $0 ~ /^SELFTEST spi FAIL status=[0-9]+$/)                 { ok = 1; fail++ }
    else if (name == "sd"     && $0 ~ /^SELFTEST sd OK type=(V1|V2|V2HC) sig=[0-9A-F][0-9A-F][0-9A-F][0-9A-F]$/) { ok = 1; pass++ }
    else if (name == "sd"     && $0 ~ /^SELFTEST sd FAIL step=(init|read) status=[0-9]+$/) { ok = 1; fail++ }
    else if (name == "sd"     && $0 ~ /^SELFTEST sd FAIL step=signature sig=[0-9A-F][0-9A-F][0-9A-F][0-9A-F]$/) { ok = 1; fail++ }
    else if (name ~ /^servo[12]$/ && $0 ~ /^SELFTEST servo[12] OK pulse_us=[0-9]+$/)       { ok = 1; pass++ }
    else if (name ~ /^servo[12]$/ && $0 ~ /^SELFTEST servo[12] FAIL status=[0-9]+$/)       { ok = 1; fail++ }
    if (!ok) bad("line " n " does not match the format: " $0)
    next
}
END {
    if (failed_format) exit 2
    if (!begun) bad("no SELFTEST BEGIN version=1 line")
    if (!ended) bad("no SELFTEST END line (report cut off?)")
    if (n != 5) bad("expected 5 check lines, found " n)
    if (end_total != 5 || end_passed != pass || end_failed != fail)
        bad("END line says total=" end_total " passed=" end_passed " failed=" end_failed \
            " but the check lines give passed=" pass+0 " failed=" fail+0)
    if (fail > 0) { print "SELFTEST FAIL (" pass "/5 passed)"; exit 1 }
    print "SELFTEST PASS (5/5 checks, report well-formed)"
}'
