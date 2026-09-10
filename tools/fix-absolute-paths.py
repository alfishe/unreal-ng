#!/usr/bin/env python3
"""Replace absolute repository paths with repo-root-relative paths.

Project convention: documentation, Python scripts and code comments must
reference repository files relative to the repository root only — never
through machine-specific absolute paths (see
docs/guidelines/coding-guidelines.md, "Paths in Documentation and Scripts").

For every configured repository root this tool rewrites:

    file://<root>/some/path   ->  some/path
    <root>/some/path          ->  some/path
    file://<root>             ->  .
    <root>                    ->  .

The current repository root is always included automatically. Historical
mount points of the same checkout (moved/renamed clones) can be passed
with --root.

What the tool deliberately does NOT touch:

    - Vendored third-party code under 3rdparty/ directories
    - Absolute paths pointing outside the repository (other projects,
      knowledge artifacts, system locations such as tool-created runtime
      mount points). Those need manual judgement per the convention:
      describe the artifact by name and its path within that project.

Usage:
    # Dry run over tracked markdown files (default, exit code 1 if found)
    python3 tools/fix-absolute-paths.py

    # Include a historical checkout location
    python3 tools/fix-absolute-paths.py --root /old/mount/point/unreal-ng

    # Scan python files as well, then apply
    python3 tools/fix-absolute-paths.py --ext md --ext py --apply

    # Scan specific files instead of tracked-tree discovery
    python3 tools/fix-absolute-paths.py --path docs/some-doc.md --apply
"""

import argparse
import subprocess
import sys
from pathlib import Path

# Replacement rules per root: longest/most-specific prefix first.
# A bare root (no trailing slash) denotes the repository itself -> '.'
REPLACEMENT_RULES = (
    ("file://{root}/", ""),
    ("{root}/", ""),
    ("file://{root}", "."),
    ("{root}", "."),
)

EXCLUDED_PATH_PARTS = ("3rdparty",)


def find_project_root() -> Path:
    """Locate the repository root from the script location, then from CWD."""
    candidates = [Path(__file__).resolve().parents[1], Path.cwd()]
    for candidate in candidates:
        if (candidate / "CMakeLists.txt").exists() and (candidate / "core").exists():
            return candidate

        for parent in [candidate, *candidate.parents]:
            if (parent / ".git").exists() and (parent / "core").exists():
                return parent

    raise SystemExit("Could not locate the repository root (no CMakeLists.txt + core/ above this script)")


def discover_files(root: Path, extensions: list[str]) -> list[Path]:
    """Return tracked files with the requested extensions, excluding 3rdparty."""
    try:
        result = subprocess.run(["git", "-C", str(root), "ls-files"],
                                capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"git ls-files failed ({exc}); use --path to pass files explicitly")

    suffixes = {f".{ext.lstrip('.')}" for ext in extensions}
    files = []
    for line in result.stdout.splitlines():
        path = root / line
        if path.suffix in suffixes and not any(part in EXCLUDED_PATH_PARTS for part in path.parts):
            files.append(path)
    return sorted(files)


def rewrite(content: str, roots: list[str]) -> tuple[str, int]:
    """Apply all replacement rules for all roots. Returns (new content, count)."""
    count = 0
    for root in roots:
        for template, replacement in REPLACEMENT_RULES:
            prefix = template.format(root=root)
            count += content.count(prefix)
            content = content.replace(prefix, replacement)
    return content, count


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Replace absolute repository paths with repo-root-relative paths.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    parser.add_argument("--root", action="append", default=[],
                        help="Additional repository root to strip (repeatable), "
                             "e.g. a historical mount point of this checkout")
    parser.add_argument("--ext", action="append", default=["md"],
                        help="File extension to scan (repeatable, default: md)")
    parser.add_argument("--path", action="append", default=[],
                        help="Scan this specific file instead of the tracked tree (repeatable)")
    parser.add_argument("--apply", action="store_true",
                        help="Write changes; without it the tool only reports (dry run)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Show an example affected line per file")
    args = parser.parse_args()
    args.ext = list(dict.fromkeys(args.ext))  # dedupe against defaults

    project_root = find_project_root()

    # Roots: current checkout first, then caller-provided historical ones
    roots = [str(project_root), *args.root]

    files = [Path(p) for p in args.path] or discover_files(project_root, args.ext)
    print(f"Repository root: {project_root}")
    print(f"Scanning {len(files)} file(s) with extensions: {', '.join(args.ext)}")
    if not args.apply:
        print("DRY RUN — pass --apply to write changes")
    print()

    files_affected = 0
    total = 0
    for path in files:
        try:
            content = path.read_text(encoding="utf-8")
        except OSError as exc:
            print(f"  Warning: cannot read {path}: {exc}", file=sys.stderr)
            continue

        new_content, count = rewrite(content, roots)
        if count == 0:
            continue

        files_affected += 1
        total += count
        relative = path.relative_to(project_root) if path.is_relative_to(project_root) else path
        print(f"{relative}: {count} occurrence(s)")
        if args.verbose:
            for old_line, new_line in zip(content.splitlines(), new_content.splitlines()):
                if old_line != new_line:
                    print(f"    - {old_line.strip()[:100]}")
                    break

        if args.apply:
            path.write_text(new_content, encoding="utf-8")

    print()
    print(f"Files with absolute paths: {files_affected}, total occurrences: {total}")
    if total and not args.apply:
        print("Run again with --apply to fix.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
