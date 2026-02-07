#!/usr/bin/env python3
"""
OpenAPI Coverage Verification Script

Ensures that every route registered in emulator_api.h and interpreter_api.h
has a corresponding path definition in the OpenAPI .inc specification files.

Usage:
    python3 verify_openapi_coverage.py [--strict]

Exit codes:
    0 - All registered routes are documented
    1 - Missing routes found (routes registered but not documented)

The --strict flag treats "extra" OpenAPI routes (documented but not registered)
as errors too. Without --strict, extras are reported as warnings only.
"""

import argparse
import glob
import os
import re
import sys

# Paths relative to project root
WEBAPI_SRC = os.path.join("core", "automation", "webapi", "src")
EMULATOR_API_H = os.path.join(WEBAPI_SRC, "emulator_api.h")
INTERPRETER_API_H = os.path.join(WEBAPI_SRC, "api", "interpreter_api.h")
OPENAPI_INC_DIR = os.path.join(WEBAPI_SRC, "openapi")

# Routes to skip (meta-endpoints, not part of API surface)
SKIP_ROUTES = {"/api/v1/openapi.json"}


def find_project_root():
    """Walk up from script location to find project root (contains CMakeLists.txt)."""
    d = os.path.dirname(os.path.abspath(__file__))
    for _ in range(10):
        if os.path.exists(os.path.join(d, "CMakeLists.txt")):
            return d
        d = os.path.dirname(d)
    # Fallback: try from cwd
    d = os.getcwd()
    for _ in range(10):
        if os.path.exists(os.path.join(d, "CMakeLists.txt")):
            return d
        d = os.path.dirname(d)
    return None


def extract_registered_routes(project_root):
    """Extract all routes from ADD_METHOD_TO declarations in header files."""
    routes = set()
    headers = [
        os.path.join(project_root, EMULATOR_API_H),
        os.path.join(project_root, INTERPRETER_API_H),
    ]
    for hdr in headers:
        if not os.path.exists(hdr):
            print(f"WARNING: Header not found: {hdr}")
            continue
        with open(hdr) as f:
            content = f.read()
        for m in re.finditer(r'"/api/v1/[^"]*"', content):
            route = m.group().strip('"')
            if route not in SKIP_ROUTES:
                routes.add(route)
    return routes


def extract_openapi_paths(project_root):
    """Extract all paths documented in OpenAPI .inc files."""
    paths = set()
    inc_dir = os.path.join(project_root, OPENAPI_INC_DIR)
    if not os.path.exists(inc_dir):
        print(f"ERROR: OpenAPI .inc directory not found: {inc_dir}")
        sys.exit(2)
    for inc in sorted(glob.glob(os.path.join(inc_dir, "*.inc"))):
        with open(inc) as f:
            content = f.read()
        for m in re.finditer(r'paths\["(/api/v1/[^"]*)"\]', content):
            paths.add(m.group(1))
    return paths


def main():
    parser = argparse.ArgumentParser(description="Verify OpenAPI specification coverage")
    parser.add_argument("--strict", action="store_true",
                        help="Treat extra (undocumented) OpenAPI routes as errors")
    args = parser.parse_args()

    project_root = find_project_root()
    if not project_root:
        print("ERROR: Could not find project root (no CMakeLists.txt found)")
        sys.exit(2)

    registered = extract_registered_routes(project_root)
    documented = extract_openapi_paths(project_root)

    missing = sorted(registered - documented)
    extra = sorted(documented - registered)

    print(f"Project root: {project_root}")
    print(f"Registered routes: {len(registered)}")
    print(f"OpenAPI paths:     {len(documented)}")
    print()

    exit_code = 0

    if missing:
        print("FAIL: Routes registered but MISSING from OpenAPI spec:")
        for r in missing:
            print(f"  ✗ {r}")
        exit_code = 1
    else:
        print("✓ All registered routes are documented in OpenAPI spec")

    print()

    if extra:
        label = "FAIL" if args.strict else "WARNING"
        print(f"{label}: Routes in OpenAPI spec but NOT registered (planned/stale):")
        for r in extra:
            symbol = "✗" if args.strict else "?"
            print(f"  {symbol} {r}")
        if args.strict:
            exit_code = 1
    else:
        print("✓ No extra routes in OpenAPI spec")

    print()
    print(f"Coverage: {len(registered - set(missing))}/{len(registered)} "
          f"({100 * len(registered - set(missing)) / len(registered):.0f}%)")

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
