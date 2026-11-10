"""table.py <filters> <score files...>: pursuit / fixation per scene and render (truth60, native, the filters in the
comma list), the later files override"""
import sys, collections
filters = sys.argv[1].split(",")
rows = collections.OrderedDict()
for path in sys.argv[2:]:
    for line in open(path):
        f = line.split()
        if len(f) != 5 or not f[1][0].isdigit():
            continue
        scene = f[0].split("/")[0]
        name = f[0].split("/")[-1].replace(scene, "native").replace("native-truth60", "truth60")
        rows.setdefault(scene, {})[name] = (float(f[1]), float(f[3]))
cols = ["truth60", "native"] + filters
print(f"{'scene':10s}" + "".join(f"{c:>16s}" for c in cols))
tot = collections.Counter()
for s, r in rows.items():
    print(f"{s:10s}" + "".join(f"{r[c][0]:8.0f}/{r[c][1]:<7.0f}" if c in r else " " * 16 for c in cols))
    for c in cols:
        if c in r:
            tot[c + "p"] += r[c][0]; tot[c + "f"] += r[c][1]
print(f"{'sum':10s}" + "".join(f"{tot[c + 'p']:8.0f}/{tot[c + 'f']:<7.0f}" for c in cols))
