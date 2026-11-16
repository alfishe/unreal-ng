"""Shared fixtures: the paths of testdata/ttd/branches and the library on sys.path."""

import json
import sys
from pathlib import Path

import pytest

TOOL_DIR = Path(__file__).resolve().parents[1]
PROJECT_ROOT = Path(__file__).resolve().parents[4]
FIXTURES = PROJECT_ROOT / "testdata" / "ttd" / "branches"
sys.path.insert(0, str(TOOL_DIR))


@pytest.fixture(scope="session")
def expected():
    return json.loads((FIXTURES / "expected.json").read_text())


@pytest.fixture(scope="session")
def fixtures():
    return FIXTURES
