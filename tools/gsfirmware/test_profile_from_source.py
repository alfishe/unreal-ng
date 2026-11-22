#!/usr/bin/env python3
"""The committed generated profiles equal what the sources give (gs-debugger design §8A.9 M8)."""

import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


class ProfileFromSourceTest(unittest.TestCase):
    def test_committed_profiles_regenerate_equal(self):
        result = subprocess.run([sys.executable, os.path.join(HERE, "profile_from_source.py")], capture_output=True,
                                text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
