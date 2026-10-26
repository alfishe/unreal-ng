#!/usr/bin/env python3
"""Unit tests for score_draft.py on tiny synthetic fixtures (testdata/)."""
import io
import json
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import score_draft as sd  # noqa: E402

DATA = os.path.join(HERE, 'testdata')


def path(name):
    return os.path.join(DATA, name)


class DefSizeTest(unittest.TestCase):
    def test_sizes(self):
        self.assertEqual(sd.def_size('DEFB 1,2,3'), 3)
        self.assertEqual(sd.def_size('DEFW $8000,0'), 4)
        self.assertEqual(sd.def_size('DEFS 512'), 512)
        self.assertEqual(sd.def_size('DEFS $20,255'), 32)
        self.assertEqual(sd.def_size('DEFM "AB",13'), 3)
        self.assertEqual(sd.def_size(r'DEFM "\"a\\",1'), 4)
        self.assertEqual(sd.def_size('DEFM "a,b;c"'), 5)
        self.assertIsNone(sd.def_size('LD A,1'))

    def test_comment_split_keeps_quoted_semicolon(self):
        self.assertEqual(sd.strip_comment('DEFM "H;",13 ; note'), ('DEFM "H;",13', 'note'))


class ParseTest(unittest.TestCase):
    def test_isub_block_skipped_by_default(self):
        ref = sd.build_skool(path('ref.skool'))
        self.assertNotIn(32760, [e.address for e in ref.entries])
        ref_isub = sd.build_skool(path('ref.skool'), isub=True)
        self.assertIn(32760, [e.address for e in ref_isub.entries])

    def test_ref_byte_map_and_sections(self):
        ref = sd.build_skool(path('ref.skool'))
        self.assertEqual(ref.entry_cat[0x8000], 'code')
        self.assertEqual(ref.entry_cat[0x8002], 'code')
        self.assertEqual(ref.entry_cat[0x8003], 'data')
        self.assertEqual(ref.entry_cat[0x8025], 'text')
        self.assertEqual(ref.entry_cat[0x802F], 'unused')
        self.assertNotIn(0x8030, ref.entry_cat)
        self.assertEqual([(a, b) for a, b, _ in ref.graphic_claims], [(0x8003, 0x8013), (0x8013, 0x8023)])

    def test_hex_skool_draft(self):
        d = sd.load_draft(path('draft.skool'))
        self.assertEqual(d.format, 'skool')
        self.assertEqual(d.line_cat[0x8013], 'text')
        self.assertEqual(d.line_cat[0x801C], 'text')
        self.assertEqual(d.line_cat[0x801D], 'data')

    def test_ctl_draft(self):
        d = sd.load_draft(path('draft.ctl'))
        self.assertEqual(d.format, 'ctl')
        self.assertEqual(d.entry_cat[0x8013], 'data')
        self.assertEqual(d.line_cat[0x8013], 'text')
        self.assertEqual(d.entry_cat[0x802C], 'unknown')


class ScoreTest(unittest.TestCase):
    def test_ctl_scores(self):
        rep = sd.score(path('draft.ctl'), path('ref.skool'), expected_path=path('expected.json'),
                       fields_path=path('fields.json'), geometry_path=path('geometry.json'))
        self.assertEqual(rep['range'], [0x8000, 0x8030])
        ce = rep['categories_entry']
        self.assertEqual((ce['bytes_agree'], ce['bytes_scored']), (42, 48))
        self.assertEqual(ce['confusion']['data']['code'], 2)
        self.assertEqual(ce['confusion']['unknown']['unused'], 4)
        self.assertEqual(rep['categories_line']['bytes_agree'], 26)
        self.assertEqual(rep['code_entries']['matched'], 1)
        self.assertEqual(rep['code_entries']['misses'], ['$802A'])
        g = rep['graphics']
        self.assertEqual((g['total'], g['covered'], g['with_draft_boundary']), (2, 2, 2))
        self.assertEqual((g['geometry_checked'], g['geometry_match']), (2, 1))
        rec = rep['records']
        self.assertEqual(rec['complete'], 0)
        self.assertEqual(rec['records'][0]['matched'], 2)
        self.assertEqual(rec['records'][0]['layouts'][0]['missing'], ['+3/1 length'])

    def test_skool_scores_and_na(self):
        rep = sd.score(path('draft.skool'), path('ref.skool'), expected_path=path('expected.json'))
        self.assertEqual(rep['categories_entry']['agree_pct'], 100.0)
        self.assertEqual(rep['categories_line']['bytes_agree'], 48 - 10)
        self.assertEqual(rep['code_entries']['matched'], 2)
        self.assertIsNone(rep['records']['complete'])
        self.assertEqual(rep['graphics']['geometry_checked'], 0)
        self.assertEqual(rep['graphics']['with_draft_boundary'], 1)

    def test_split_vars(self):
        rep = sd.score(path('draft.skool'), path('ref.skool'), start=0x8000, end=0x8030, split_vars=True)
        self.assertEqual(rep['categories_entry']['confusion']['vars']['vars'], 4)

    def test_cli_text_and_json(self):
        out = io.StringIO()
        old = sys.stdout
        sys.stdout = out
        try:
            sd.main([path('draft.ctl'), path('ref.skool'), '--expected', path('expected.json'), '--json', '-'])
        finally:
            sys.stdout = old
        rep = json.loads(out.getvalue())
        self.assertEqual(rep['records']['status'], 'n/a (draft provides no fields)')
        text = io.StringIO()
        sd.report_text(rep, text)
        self.assertIn('Code entries: 1 of 2', text.getvalue())


class ExpectedFileTest(unittest.TestCase):
    def test_manic_miner_fields_tile_each_record(self):
        exp = sd.load_json(os.path.join(HERE, 'expected', 'manic-miner-records.json'))
        self.assertEqual([r['name'] for r in exp['records']],
                         ['cavern_buffer', 'guardians', 'items', 'conveyor', 'willy_state'])
        for rec in exp['records']:
            for layout in rec['layouts']:
                pos = 0
                for f in layout['fields']:
                    self.assertEqual(f['offset'], pos, (rec['name'], f))
                    pos += f['size']
                self.assertEqual(pos, rec['size'], rec['name'])


if __name__ == '__main__':
    unittest.main()
