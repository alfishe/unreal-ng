#!/usr/bin/env python3
"""POC 053: self time per function in the threads of a macOS `sample` report that contain <marker>.

usage: sample_self.py <sample.txt> <marker> [top]
"""
import collections
import re
import sys

path, marker = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
lines = open(path).read().split("\n")
starts = [i for i, l in enumerate(lines) if re.match(r"^\s+\d+ Thread_", l)]
end = next(i for i, l in enumerate(lines) if l.startswith("Total number in stack"))
node_re = re.compile(r"^([\s+!:|]*)(\d+) (.+?)  \(in ")
for k, s in enumerate(starts):
    e = starts[k + 1] if k + 1 < len(starts) else end
    blk = lines[s:e]
    if not any(marker in l for l in blk):
        continue
    nodes = []
    for l in blk[1:]:
        m = node_re.match(l)
        if m:
            nodes.append((len(m.group(1)), int(m.group(2)), m.group(3)))
    selfc = collections.Counter()
    for i, (d, c, n) in enumerate(nodes):
        kids = 0
        j = i + 1
        child_depth = None
        while j < len(nodes) and nodes[j][0] > d:
            if child_depth is None:
                child_depth = nodes[j][0]
            if nodes[j][0] == child_depth:
                kids += nodes[j][1]
            j += 1
        selfc[n] += c - kids
    busy = {n: c for n, c in selfc.items() if not re.search(r"cvwait|semwait|swtch_pri|kevent|poll$|__workq", n)}
    tot = sum(busy.values())
    print("%s  busy samples %d" % (lines[s].strip()[:60], tot))
    for n, c in sorted(busy.items(), key=lambda kv: -kv[1])[:top]:
        print("%6d %5.1f%%  %s" % (c, 100.0 * c / tot, n[:110]))
