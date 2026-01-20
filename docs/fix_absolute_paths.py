#!/usr/bin/env python3
"""
Fix absolute file:// paths in markdown documentation files.

This script finds and replaces absolute paths like:
    file:///Volumes/MyVolume/Projects/unreal-ng/path/to/file
with relative paths:
    path/to/file

Usage:
    python fix_absolute_paths.py              # Apply fixes
    python fix_absolute_paths.py --dry-run    # Preview changes without modifying files
    python fix_absolute_paths.py -n           # Same as --dry-run
"""

import argparse
import os
import re
import sys
from pathlib import Path
from typing import NamedTuple


class FileReport(NamedTuple):
    """Report for a single file."""
    path: Path
    occurrences: int
    patterns_found: list[str]


def find_project_root() -> Path:
    """Find the project root by looking for known markers."""
    script_dir = Path(__file__).resolve().parent

    # Script is in /docs, so parent is project root
    project_root = script_dir.parent

    # Verify we found the right place
    if (project_root / "core").exists() or (project_root / "docs").exists():
        return project_root

    # Fallback: walk up looking for .git
    current = script_dir
    while current != current.parent:
        if (current / ".git").exists():
            return current
        current = current.parent

    return project_root


def build_pattern(project_root: Path) -> re.Pattern:
    """
    Build regex pattern to match absolute file:// URLs pointing to project.

    Matches patterns like:
        file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/
        file:///home/user/projects/unreal-ng/
        file:///C:/Projects/unreal-ng/
    """
    # Escape the project root path for regex
    root_str = str(project_root)
    escaped_root = re.escape(root_str)

    # Pattern: file:// followed by project root path followed by /
    # The trailing content (relative path) is captured
    pattern = rf'file://{escaped_root}/'

    return re.compile(pattern)


def scan_file(filepath: Path, pattern: re.Pattern) -> FileReport:
    """Scan a file for absolute path occurrences."""
    try:
        content = filepath.read_text(encoding='utf-8')
    except Exception as e:
        print(f"  Warning: Could not read {filepath}: {e}", file=sys.stderr)
        return FileReport(filepath, 0, [])

    matches = pattern.findall(content)

    # Find unique patterns for reporting
    all_matches = list(pattern.finditer(content))
    unique_patterns = []
    for match in all_matches:
        # Get some context around the match
        start = max(0, match.start() - 10)
        end = min(len(content), match.end() + 50)
        context = content[start:end].split('\n')[0]  # First line only
        if context not in unique_patterns:
            unique_patterns.append(context)

    return FileReport(filepath, len(all_matches), unique_patterns[:5])  # Limit to 5 examples


def fix_file(filepath: Path, pattern: re.Pattern, dry_run: bool) -> int:
    """Fix absolute paths in a file. Returns number of replacements made."""
    try:
        content = filepath.read_text(encoding='utf-8')
    except Exception as e:
        print(f"  Error: Could not read {filepath}: {e}", file=sys.stderr)
        return 0

    # Count occurrences before replacement
    occurrences = len(pattern.findall(content))

    if occurrences == 0:
        return 0

    if dry_run:
        return occurrences

    # Replace absolute paths with relative (empty replacement removes the prefix)
    new_content = pattern.sub('', content)

    try:
        filepath.write_text(new_content, encoding='utf-8')
    except Exception as e:
        print(f"  Error: Could not write {filepath}: {e}", file=sys.stderr)
        return 0

    return occurrences


def find_markdown_files(search_dir: Path) -> list[Path]:
    """Find all markdown files in directory and subdirectories."""
    return sorted(search_dir.rglob("*.md"))


def main():
    parser = argparse.ArgumentParser(
        description="Fix absolute file:// paths in markdown documentation files.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        '-n', '--dry-run',
        action='store_true',
        help="Preview changes without modifying files"
    )
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help="Show detailed information about each match"
    )
    parser.add_argument(
        '--search-dir',
        type=Path,
        default=None,
        help="Directory to search (default: docs relative to project root)"
    )

    args = parser.parse_args()

    # Find project root
    project_root = find_project_root()
    print(f"Project root: {project_root}")

    # Determine search directory
    if args.search_dir:
        search_dir = args.search_dir.resolve()
    else:
        search_dir = project_root / "docs"

    if not search_dir.exists():
        print(f"Error: Search directory does not exist: {search_dir}", file=sys.stderr)
        sys.exit(1)

    print(f"Search directory: {search_dir}")

    # Build the pattern
    pattern = build_pattern(project_root)
    print(f"Pattern: {pattern.pattern}")
    print()

    # Find all markdown files
    md_files = find_markdown_files(search_dir)
    print(f"Found {len(md_files)} markdown files")
    print()

    if args.dry_run:
        print("=" * 60)
        print("DRY RUN MODE - No files will be modified")
        print("=" * 60)
        print()

    # Process files
    total_files_affected = 0
    total_occurrences = 0

    for filepath in md_files:
        report = scan_file(filepath, pattern)

        if report.occurrences > 0:
            total_files_affected += 1
            total_occurrences += report.occurrences

            relative_path = filepath.relative_to(project_root)
            print(f"{'[DRY RUN] ' if args.dry_run else ''}File: {relative_path}")
            print(f"  Occurrences: {report.occurrences}")

            if args.verbose and report.patterns_found:
                print("  Examples:")
                for example in report.patterns_found:
                    # Truncate long examples
                    if len(example) > 80:
                        example = example[:77] + "..."
                    print(f"    - {example}")

            if not args.dry_run:
                fixed = fix_file(filepath, pattern, dry_run=False)
                print(f"  Fixed: {fixed} occurrences")

            print()

    # Summary
    print("=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"Total files scanned: {len(md_files)}")
    print(f"Files with absolute paths: {total_files_affected}")
    print(f"Total occurrences: {total_occurrences}")

    if args.dry_run:
        if total_occurrences > 0:
            print()
            print("Run without --dry-run to apply fixes.")
    else:
        if total_occurrences > 0:
            print()
            print("All fixes applied successfully.")
        else:
            print()
            print("No absolute paths found. Nothing to fix.")

    return 0 if total_occurrences == 0 or not args.dry_run else 1


if __name__ == "__main__":
    sys.exit(main())
