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


MM_SKOOL = '/Volumes/TB4-4Tb/Projects/emulators/github/skoolkid-manicminer/sources/mm.skool'


class MacroGeometryTest(unittest.TestCase):
    def test_udgarray_counts_like_skoolkit(self):
        self.assertEqual(sd.udg_count('32771-32786-1-16', 2), 2)
        self.assertEqual(sd.udg_count('32768-32799-1-16', 2), 4)
        self.assertEqual(sd.udg_count('100;101;102x3', 2), 5)
        self.assertEqual(sd.udg_count('40000-40031-8', 1), 4)

    def test_defs_for_and_let_expand(self):
        defs = sd.parse_defs('#DEF(#PAIR(a)\n  #LET(b=$a+8)\n  #UDG({b}) | #UDG({b}+8)\n)\n'
                             '#DEF(#ROW(n) #FOR(1,$n)(k,#UDG(k), | ))\n')
        self.assertEqual(set(defs), {'PAIR', 'ROW'})
        text = sd.expand_macros('{ #PAIR1000 }', defs)
        self.assertEqual(text.count('#UDG('), 2)
        self.assertIn('#UDG(1008)', text)
        self.assertEqual(sd.expand_macros('#ROW3', defs).count('#UDG'), 3)

    def test_section_shapes(self):
        self.assertEqual(sd.section_geometry('#UDGTABLE { #UDG1 | #UDG9 | #UDG17 } TABLE#', 24, {}),
                         {'kind': 'graphic', 'width': 8, 'height': 8, 'frames': 3})
        self.assertEqual(sd.section_geometry('#UDGTABLE { #UDG1 } TABLE# #UDGTABLE { #UDG2 | #UDG3 } TABLE#', 8, {}),
                         {'kind': 'graphic', 'width': 8, 'height': 8, 'frames': 1})
        self.assertEqual(sd.section_geometry('#SCR(1,0,0,32,8) #SCR(1,0,8,32,8)', 4096, {}),
                         {'kind': 'screen', 'width': 256, 'height': 128, 'frames': 1})
        self.assertEqual(sd.section_geometry('#UDGARRAY32,,,(1-512)(*bg) #OVER(0,0)(sprite)', 512, {}),
                         {'kind': 'map', 'width': 32, 'height': 16, 'frames': 1})
        self.assertIsNone(sd.section_geometry('no picture', 8, {}))

    def test_geometry_lookup_prefers_largest_overlap(self):
        geometry = [{'start': '$5E00', 'end': '$5E02'}, {'start': '$5E00', 'end': '$6000', 'width': 32}]
        self.assertEqual(sd.geometry_lookup(geometry, 0x5E00, 0x6000)['width'], 32)

    @unittest.skipUnless(os.path.exists(MM_SKOOL), 'skoolkid Manic Miner sources not present')
    def test_manic_miner_macros_give_the_handwritten_geometry(self):
        ref = sd.build_skool(MM_SKOOL)
        defs = sd.load_defs(sd.default_refs(MM_SKOOL), MM_SKOOL)
        exp = sd.load_json(os.path.join(HERE, 'expected', 'manic-miner-records.json'))['graphics']
        self.assertEqual(len(exp), 106)
        self.assertEqual(len(ref.graphic_claims), 106)
        for g in exp:
            start, end = sd.addr_of(g['start']), sd.addr_of(g['end'])
            got = sd.section_geometry(ref.graphic_text.get(start, ''), end - start, defs)
            self.assertIsNotNone(got, g['start'])
            self.assertEqual((got['width'], got['height'], got['frames']),
                             (g['width'], g['height'], g['frames']), g['start'])

    def test_macro_geometry_mode_scores_without_expected(self):
        rep = sd.score(path('draft.ctl'), path('ref.skool'), geometry_path=path('geometry.json'), ref_files=[])
        g = rep['graphics']
        self.assertEqual((g['geometry_known'], g['geometry_match']), (2, 1))
        self.assertEqual(g['geometry_match_pct'], 50.0)
        self.assertEqual(g['regions'][1]['reference_geometry']['width'], 16)


if __name__ == '__main__':
    unittest.main()
