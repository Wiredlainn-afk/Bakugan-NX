#!/usr/bin/env python3
"""Turns the "target not in any function" lines of a rexglue codegen log into [functions] entries of overrides.toml.

Each line "0xTARGET from 0xSOURCE: b 0xTARGET ... - target not in any function" becomes
    "0xTARGET" = { }  # from 0xSOURCE[, 0xSOURCE2 ...]
Entries already in the file are kept untouched (their callers are merged into the comment is NOT attempted: the first
evidence stays). New entries are appended under a dated comment, which is the evidence line the manifest requires.

Usage: python3 -I tools/log_to_overrides.py out/codegen.log app/overrides.toml "<note: which run found them>"
"""
import re, sys, collections

log, toml, note = sys.argv[1], sys.argv[2], sys.argv[3]
pat = re.compile(r"^\s*0x([0-9A-F]{8}) from 0x([0-9A-F]{8}): (\w+) 0x[0-9A-F]{8}.* - target not in any function")
found = collections.OrderedDict()
for line in open(log, encoding="utf-8", errors="replace"):
    m = pat.match(line)
    if m:
        found.setdefault(m.group(1), []).append(m.group(2))
text = open(toml, encoding="utf-8").read()
have = set(m.group(1).upper() for m in re.finditer(r'^"0x([0-9A-Fa-f]{8})"', text, re.M))
new = {a: s for a, s in found.items() if a not in have}
if not new:
    print("nothing new (%d targets in the log, all present)" % len(found)); sys.exit(0)
out = ["", "# %s: %d direct branches (`b`) to addresses automatic discovery had not assigned to any function" % (note, len(new)),
       "# (\"target not in any function\"); the callers are listed. No sizes imposed."]
for a in sorted(new):
    out.append('"0x%s" = { }  # from %s' % (a, ", ".join("0x" + s for s in sorted(set(new[a])))))
open(toml, "a", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
print("added %d entries" % len(new))
