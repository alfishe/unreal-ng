#!/usr/bin/env python3
"""Replace absolute and invalid root-relative paths with document-relative links.

Project convention: documentation, Python scripts and code comments must
reference repository files using relative paths from the document's location
(or relative to repo root where appropriate) — never through machine-specific
absolute paths (see docs/guidelines/coding-guidelines.md).

For every document scanned:
1. Absolute paths pointing into any configured repo root are rewritten to document-relative paths:
    file://<root>/path/to/target.md  ->  relative path from current file to target.md
    <root>/path/to/target.md         ->  relative path from current file to target.md

2. Root-relative paths in Markdown links [text](path/to/target.md) inside subfolders are rewritten:
    docs/inprogress/.../target.md    ->  relative path from current file to target.md

Every proposed relative path is strictly verified against filesystem existence
`(doc_dir / rel).exists()` prior to applying link rewrites.

Usage:
    # Dry run over tracked & untracked markdown files (default, exit code 1 if found)
    python3 tools/fix-absolute-paths.py

    # Include a historical checkout location
    python3 tools/fix-absolute-paths.py --root /old/mount/point/unreal-ng

    # Scan python files as well, then apply
    python3 tools/fix-absolute-paths.py --ext md --ext py --apply

    # Scan specific files or directories
    python3 tools/fix-absolute-paths.py --path docs/inprogress/2026-09-21-devtools-roadmap --apply
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

EXCLUDED_PATH_PARTS = ("3rdparty", ".git", "cmake-build-release", "scratch", "build")


def find_project_root() -> Path:
    """Locate the repository root from the script location, then from CWD."""
    candidates = [Path(__file__).resolve().parents[1], Path.cwd()]
    for candidate in candidates:
        if (candidate / "CMakeLists.txt").exists() and (candidate / "core").exists():
            return candidate.resolve()

        for parent in [candidate, *candidate.parents]:
            if (parent / ".git").exists() and (parent / "core").exists():
                return parent.resolve()

    raise SystemExit("Could not locate the repository root (no CMakeLists.txt + core/ above this script)")


def discover_files(root: Path, extensions: list[str]) -> list[Path]:
    """Return tracked and untracked (non-ignored) files with requested extensions, excluding 3rdparty."""
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard"],
            capture_output=True, text=True, check=True
        )
        lines = result.stdout.splitlines()
    except (OSError, subprocess.CalledProcessError):
        lines = [str(p.relative_to(root)) for p in root.rglob("*") if p.is_file()]

    suffixes = {f".{ext.lstrip('.')}" for ext in extensions}
    files = []
    for line in lines:
        path = (root / line).resolve()
        if path.suffix in suffixes and not any(part in EXCLUDED_PATH_PARTS for part in path.parts):
            files.append(path)
    return sorted(files)


def expand_paths(paths: list[str], extensions: list[str]) -> list[Path]:
    """Expand explicit file or directory arguments."""
    suffixes = {f".{ext.lstrip('.')}" for ext in extensions}
    expanded = []
    for p_str in paths:
        p = Path(p_str).resolve()
        if p.is_dir():
            for f in p.rglob("*"):
                if f.is_file() and f.suffix in suffixes and not any(part in EXCLUDED_PATH_PARTS for part in f.parts):
                    expanded.append(f.resolve())
        elif p.is_file():
            expanded.append(p.resolve())
        else:
            print(f"Warning: path not found: {p_str}", file=sys.stderr)
    return sorted(expanded)


def rewrite_file_content(content: str, doc_path: Path, roots: list[Path]) -> tuple[str, int]:
    """Rewrite absolute root paths and invalid root-relative markdown links to document-relative paths.

    Target file existence is strictly verified at `(doc_dir / rel).exists()`.
    """
    count = 0
    new_content = content
    doc_dir = doc_path.parent.resolve()
    primary_root = roots[0].resolve()

    # Step 1: Rewrite explicit absolute paths (file://<root>/... or <root>/...)
    for root in roots:
        root_resolved = root.resolve()
        r_str = str(root_resolved)

        pattern = re.compile(
            r'(?:file://)?' + re.escape(r_str) + r'(?:/([^#\s\)\"\'\>\`]*))?(#[^\s\)\"\'\>\`]*)?'
        )

        def replace_abs_match(match: re.Match) -> str:
            nonlocal count
            subpath = match.group(1) or ""
            anchor = match.group(2) or ""

            target_path = (root_resolved / subpath).resolve() if subpath else root_resolved
            try:
                rel = os.path.relpath(target_path, doc_dir).replace("\\", "/")
            except ValueError:
                rel = str(target_path).replace("\\", "/")

            if rel == "":
                rel = "."

            # Mandatory verification: ensure target file exists on disk
            proposed_target = (doc_dir / rel).resolve()
            if not proposed_target.exists():
                print(
                    f"  Warning: in {doc_path.name}: target path does not exist on disk ({proposed_target})",
                    file=sys.stderr
                )

            count += 1
            return rel + anchor

        new_content = pattern.sub(replace_abs_match, new_content)

    # Step 2: Fix markdown links [text](target) where target is root-relative instead of doc-relative
    markdown_link_pattern = re.compile(r'\[([^\]]+)\]\(([^)\s]+)\)')

    def replace_md_link(match: re.Match) -> str:
        nonlocal count
        text = match.group(1)
        target = match.group(2)

        # Ignore external protocols
        if target.startswith(("http://", "https://", "mailto:", "ftp:", "data:")):
            return match.group(0)

        if "#" in target:
            path_part, anchor = target.split("#", 1)
            anchor = "#" + anchor
        else:
            path_part, anchor = target, ""

        if not path_part:
            return match.group(0)

        # Check if path_part resolves relative to doc_dir
        doc_rel = (doc_dir / path_part).resolve()
        if not doc_rel.exists():
            # Try resolving relative to primary_root
            root_rel = (primary_root / path_part.lstrip("/")).resolve()
            if root_rel.exists():
                rel = os.path.relpath(root_rel, doc_dir).replace("\\", "/")
                if rel == "":
                    rel = "."

                # Mandatory verification: verify that (doc_dir / rel) actually exists on disk
                proposed_target = (doc_dir / rel).resolve()
                if proposed_target.exists():
                    if rel != target:
                        count += 1
                        return f"[{text}]({rel}{anchor})"
                else:
                    print(
                        f"  Warning: in {doc_path.name}: proposed target link '{rel}' does not exist on disk ({proposed_target})",
                        file=sys.stderr
                    )
        return match.group(0)

    new_content = markdown_link_pattern.sub(replace_md_link, new_content)

    return new_content, count


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Replace absolute and root-relative paths with document-relative paths.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    parser.add_argument("--root", action="append", default=[],
                        help="Additional repository root to strip (repeatable), "
                             "e.g. a historical mount point of this checkout")
    parser.add_argument("--ext", action="append", default=["md"],
                        help="File extension to scan (repeatable, default: md)")
    parser.add_argument("--path", action="append", default=[],
                        help="Scan specific file(s) or directory(ies) (repeatable)")
    parser.add_argument("--apply", action="store_true",
                        help="Write changes; without it the tool only reports (dry run)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Show an example affected line per file")
    args = parser.parse_args()
    args.ext = list(dict.fromkeys(args.ext))  # dedupe against defaults

    project_root = find_project_root()
    roots = [project_root, *[Path(r).resolve() for r in args.root]]

    if args.path:
        files = expand_paths(args.path, args.ext)
    else:
        files = discover_files(project_root, args.ext)

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

        new_content, count = rewrite_file_content(content, path, roots)
        if count == 0:
            continue

        files_affected += 1
        total += count
        try:
            relative_display = path.relative_to(project_root)
        except ValueError:
            relative_display = path

        print(f"{relative_display}: {count} occurrence(s)")
        if args.verbose:
            for old_line, new_line in zip(content.splitlines(), new_content.splitlines()):
                if old_line != new_line:
                    print(f"    - {old_line.strip()[:100]}")
                    print(f"    + {new_line.strip()[:100]}")
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
