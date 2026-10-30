#!/usr/bin/env python3
"""The A/B harness of the analysis layers (plan.md "How we measure", P5): one run's draft through a fresh zx-reaper
project and the reference score (reaper_gate.py), its token ledger against run B's, one verdict.

  run_ab.py --draft scratch/p5/mm --work scratch/p5/ab --ledger scratch/p5/run-c-ledger.json \\
      --reaper /path/to/zx-reaper --tape run-B/original/game.tap --tests run-B/tests \\
      --reference mm.skool --expected tools/verification/analysis/expected/manic-miner-records.json \\
      --venv scratch/skoolkit-venv --bin scratch/reaper-mm/bin [--run-b-skool run-B/compare/B.skool]

It runs no agent: a review-only agent works on the draft by the recipe (.recipe/analysis/reverse-engineering.md), its
draft directory and its ledger come in here.

--draft: a directory with draft.skool, draft.layout.mk, draft.fields.json, draft.geometry.json (mm_session.py's
output, or the review agent's export into the same names).

--ledger: the run's token ledger in run B's format (baselines/run-b-token-ledger.json): a top-level key per run
({"run_c": {"agents": [{"id", "tokens", "tool_calls", "wall_s", "group"?}, ...], "totals"?: {...}}}); the first run
that is not run_a / run_b is taken (--run picks one). Without --ledger, or with an empty one, the token side is
"no ledger" and only the quality side is judged.

Quality of run B: its final skool scored the same way (--run-b-skool), else the P0 baseline's numbers (score-p0.md:
categories 92.3 % by line and by entry, code entries 33 / 33, graphics covered 106 / 106; names from score-p5.md: 50 of 142 the same).

Names (score_draft.py, "Names"): each reference entry and @label in the range against the draft's label at the same
address: auto (generated), exact, normalized (the same words), needs a judge, missing. The deterministic `same`
(exact + normalized) is compared; the judge rows are listed, not counted (names-pairs.tsv in --work for a judge).

The verdict (plan.md P5 row): subagent tokens <= --token-target (200000) and quality at least run B's (byte
categories by line, code entries matched, graphics regions covered, names the same) with every gate step passing. By
line counts each reference sub-block (a cavern name is text inside a data entry); by entry, reported too, takes the
whole entry's type. ab.json and
ab.md go into --work; exit 0 when the verdict passes, 1 when it fails, 2 when the gate could not run.
"""

import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BASELINE = os.path.join(ROOT, 'docs', 'inprogress', '2026-10-24-analysis-layers', 'baselines',
                        'run-b-token-ledger.json')
RUN_B_QUALITY = {'categories': 92.3, 'categories_entry': 92.3, 'entries_matched': 33, 'entries_reference': 33, 'graphics_covered': 106,
                 'graphics_total': 106, 'names_same': 50, 'names_total': 142, 'names_judge': 61, 'names_auto': 0,
                 'names_missing': 31, 'source': 'score-p0.md, score-p5.md (run B final skool)'}


def agreement(level):
    """Percent of bytes whose category matches the reference, from a confusion table"""
    confusion = level['confusion']
    total = sum(sum(row.values()) for row in confusion.values())
    same = sum(confusion.get(c, {}).get(c, 0) for c in level['categories'])
    return round(100.0 * same / total, 2) if total else 0.0


def quality(score):
    graphics = score.get('graphics', {}).get('regions', [])
    records = score.get('records', {})
    out = {
        'categories': agreement(score['categories_line']),
        'categories_entry': agreement(score['categories_entry']),
        'entries_matched': score['code_entries']['matched'],
        'entries_reference': score['code_entries']['reference'],
        'entries_extra': len(score['code_entries'].get('extra', [])),
        'graphics_covered': sum(1 for g in graphics if g.get('status') == 'covered'),
        'graphics_total': len(graphics),
        'graphics_boundary': sum(1 for g in graphics if g.get('draft_boundary')),
        'graphics_geometry': sum(1 for g in graphics if g.get('geometry') == 'match'),
        'records_found': sum(1 for r in records.get('records', [])
                             if any(l.get('found') for l in r.get('layouts', []))) if records.get('records') else None,
        'records_total': len(records.get('records', [])) if records.get('records') else None,
    }
    names = (score.get('names') or {}).get('all')
    if names:
        out.update({'names_same': names['same'], 'names_total': names['total'], 'names_exact': names['exact'],
                    'names_normalized': names['normalized'], 'names_judge': names['judge'],
                    'names_auto': names['auto'], 'names_missing': names['missing']})
    return out


def ledger_totals(run):
    """Subagent tokens, tool calls and wall seconds of one run of a ledger; the RE part leaves the article out"""
    agents = run.get('agents') or []
    if not agents:
        totals = run.get('totals') or {}
        tokens = totals.get('tokens', totals.get('tokens_stated'))
        return {'agents': 0, 'tokens': tokens, 're_tokens': totals.get('re_part', tokens),
                'tool_calls': totals.get('tool_calls_subagents'), 'wall_s': None}
    tokens = sum(a.get('tokens', 0) for a in agents)
    article = sum(a.get('tokens', 0) for a in agents if a.get('group') == 'article')
    return {'agents': len(agents), 'tokens': tokens, 're_tokens': tokens - article,
            'tool_calls': sum(a.get('tool_calls', 0) for a in agents),
            'wall_s': sum(a.get('wall_s', 0) for a in agents)}


def pick_run(ledger, name):
    if name:
        return name, ledger.get(name)
    for key, value in ledger.items():
        if key not in ('run_a', 'run_b') and isinstance(value, dict):
            return key, value
    return None, None


def score_file(python, draft, args, out):
    cmd = [python, os.path.join(HERE, 'score_draft.py'), draft, os.path.abspath(args.reference),
           '--expected', os.path.abspath(args.expected), '--json', out]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, check=False)
    with open(out) as f:
        return json.load(f)


def names_cell(q):
    if q.get('names_same') is None:
        return 'n/a'
    total = q.get('names_total') or 0
    pct = round(100.0 * q['names_same'] / total, 2) if total else 0.0
    return f'{q["names_same"]} / {total} ({pct} %)'


def fmt(value, suffix=''):
    return 'n/a' if value is None else f'{value:,}{suffix}' if isinstance(value, int) else f'{value}{suffix}'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--draft', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--ledger', help="the run's token ledger (run B's format); none: no token side")
    ap.add_argument('--run', help='the ledger key of the run (default: the first that is not run_a / run_b)')
    ap.add_argument('--baseline', default=BASELINE, help="run B's ledger")
    ap.add_argument('--run-b-skool', help="run B's final skool, scored for the quality side")
    ap.add_argument('--token-target', type=int, default=200000)
    ap.add_argument('--skip-gate', action='store_true', help='reuse <work>/gate/gate.json and score.json')
    for name in ('reaper', 'tape', 'tests', 'reference', 'expected'):
        ap.add_argument('--' + name, required=True)
    ap.add_argument('--venv')
    ap.add_argument('--bin')
    args = ap.parse_args()

    work = os.path.abspath(args.work)
    gate_dir = os.path.join(work, 'gate')
    os.makedirs(work, exist_ok=True)
    if not args.skip_gate:
        cmd = [sys.executable, os.path.join(HERE, 'reaper_gate.py'), '--reaper', args.reaper, '--tape', args.tape,
               '--tests', args.tests, '--draft', args.draft, '--work', gate_dir, '--reference', args.reference,
               '--expected', args.expected]
        if args.venv:
            cmd += ['--venv', args.venv]
        if args.bin:
            cmd += ['--bin', args.bin]
        print('$', ' '.join(cmd), flush=True)
        subprocess.run(cmd, check=False)
    try:
        with open(os.path.join(gate_dir, 'gate.json')) as f:
            gate = json.load(f)
        with open(os.path.join(gate_dir, 'score.json')) as f:
            score = json.load(f)
    except (OSError, ValueError) as e:
        print(f'the gate did not run through: {e}', file=sys.stderr)
        return 2
    gate_ok = all(s['status'] == 0 for s in gate['steps'])
    mine = quality(score)
    if score.get('names'):
        sys.path.insert(0, HERE)
        import score_draft  # noqa: E402
        score_draft.write_pairs(os.path.join(work, 'names-pairs.tsv'), score['names']['pairs'])

    if args.run_b_skool:
        python = os.path.join(os.path.abspath(args.venv), 'bin', 'python3') if args.venv else sys.executable
        python = sys.executable if not os.path.exists(python) else python
        theirs = quality(score_file(python, os.path.abspath(args.run_b_skool), args,
                                    os.path.join(work, 'run-b-score.json')))
        theirs['source'] = os.path.abspath(args.run_b_skool)
    else:
        theirs = dict(RUN_B_QUALITY)

    with open(args.baseline) as f:
        run_b = ledger_totals(json.load(f)['run_b'])
    run_name, run = None, None
    if args.ledger:
        with open(args.ledger) as f:
            text = f.read().strip()
        run_name, run = pick_run(json.loads(text) if text else {}, args.run)
    tokens = ledger_totals(run) if run else None

    names_ok = None if 'names_same' not in mine or 'names_same' not in theirs else \
        mine['names_same'] >= theirs['names_same']
    quality_ok = (mine['categories'] >= theirs['categories'] and mine['entries_matched'] >= theirs['entries_matched']
                  and mine['graphics_covered'] >= theirs['graphics_covered'] and names_ok is not False)
    tokens_ok = None if not tokens or tokens['tokens'] is None else tokens['tokens'] <= args.token_target
    verdict = gate_ok and quality_ok and tokens_ok is not False
    report = {'draft': os.path.abspath(args.draft), 'gate_ok': gate_ok, 'gate_steps': gate['steps'],
              'quality': mine, 'run_b_quality': theirs, 'quality_ok': quality_ok, 'names_ok': names_ok,
              'run': run_name, 'tokens': tokens, 'run_b_tokens': run_b, 'token_target': args.token_target,
              'tokens_ok': tokens_ok, 'verdict': 'pass' if verdict else 'fail'}
    with open(os.path.join(work, 'ab.json'), 'w') as f:
        json.dump(report, f, indent=1)

    lines = ['# A/B: ' + os.path.basename(os.path.abspath(args.draft)) + ' against run B', '',
             f'Gate (fresh zx-reaper project, no hand fixes): {"pass" if gate_ok else "FAIL"}', '']
    for step in gate['steps']:
        short = ' '.join(re.sub(r'/\S*/', '', w) for w in step['cmd'].split())  # paths to their base names
        lines.append(f'- `{short[-90:]}`: {"ok" if step["status"] == 0 else "status " + str(step["status"])}')
    lines += ['', '| Measure | This run | Run B |', '| --- | ---: | ---: |',
              f'| byte categories (by line) | {mine["categories"]} % | {theirs["categories"]} % |',
              f'| ... by entry | {mine["categories_entry"]} % | {fmt(theirs.get("categories_entry"))} % |',
              f'| code entries | {mine["entries_matched"]} / {mine["entries_reference"]} | '
              f'{theirs["entries_matched"]} / {theirs["entries_reference"]} |',
              f'| graphics covered | {mine["graphics_covered"]} / {mine["graphics_total"]} | '
              f'{theirs["graphics_covered"]} / {theirs["graphics_total"]} |',
              f'| graphics with the right geometry | {mine["graphics_geometry"]} | {fmt(theirs.get("graphics_geometry"))} |',
              f'| records found | {fmt(mine["records_found"])} / {fmt(mine["records_total"])} | n/a |',
              f'| names the same (exact + normalized) | {names_cell(mine)} | {names_cell(theirs)} |',
              f'| ... exact / normalized | {fmt(mine.get("names_exact"))} / {fmt(mine.get("names_normalized"))} | '
              f'{fmt(theirs.get("names_exact"))} / {fmt(theirs.get("names_normalized"))} |',
              f'| ... named, needs a judge | {fmt(mine.get("names_judge"))} | {fmt(theirs.get("names_judge"))} |',
              f'| ... generated (auto) / missing | {fmt(mine.get("names_auto"))} / {fmt(mine.get("names_missing"))} | '
              f'{fmt(theirs.get("names_auto"))} / {fmt(theirs.get("names_missing"))} |',
              f'| subagent tokens | {fmt(tokens["tokens"]) if tokens else "no ledger"} | {fmt(run_b["tokens"])} |',
              f'| ... without the article | {fmt(tokens["re_tokens"]) if tokens else "no ledger"} | '
              f'{fmt(run_b["re_tokens"])} |',
              f'| subagent tool calls | {fmt(tokens["tool_calls"]) if tokens else "no ledger"} | '
              f'{fmt(run_b["tool_calls"])} |',
              '', f'Quality at least run B\'s: {"yes" if quality_ok else "no"}; tokens <= {args.token_target:,}: '
              f'{"no ledger" if tokens_ok is None else "yes" if tokens_ok else "no"}; verdict: **{report["verdict"]}**', '']
    with open(os.path.join(work, 'ab.md'), 'w') as f:
        f.write('\n'.join(lines))
    print('\n'.join(lines))
    return 0 if verdict else 1


if __name__ == '__main__':
    sys.exit(main())
