"""For each '; BLOCK name' header check a comment mentioning the name
appears within 25 lines above it (Tok* records are exempt - covered by
the TokenTable comment)."""
import re, sys
HDR = re.compile(r"; BLOCK '([^']+)'")
for path in sys.argv[1:]:
    lines = open(path).read().splitlines()
    missing = []
    for i, l in enumerate(lines):
        m = HDR.search(l)
        if not m:
            continue
        name = m.group(1)
        if name.startswith('Tok'):
            continue
        window = '\n'.join(lines[max(0, i-25):i])
        if name not in window:
            missing.append((i+1, name))
    print('%s: %d block headers, %d without comment %s' % (
        path.split('/')[-1],
        len(HDR.findall('\n'.join(lines))),
        len(missing), missing or ''))
