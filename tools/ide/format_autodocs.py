#!/usr/bin/env python3
r"""
Doxygen Comment Formatter

Converts C++ documentation comments to CLion-friendly format:
1. Changes /** ... */ block comments to /// line comments
2. Converts \brief, \param, etc. to @brief, @param etc.
3. Preserves existing /// comments and regular block comments
4. Removes unnecessary empty lines at the start / end of doc blocks

Usage:
  python format_docs.py <file-or-directory>

Examples:
  # Process single file
  python format_docs.py src/file.cpp

  # Process all C++ files recursively
  python format_docs.py src/

CLion Integration:
1. Open CLion Settings → Tools → External Tools
2. Click + to add new tool with these settings:
   - Name: Format Doxygen Docs
   - Program: python3
   - Arguments: "/path/to/format_docs.py" "$FilePath$"
   - Working Directory: "$ProjectFileDir$"
3. Optional: Assign keyboard shortcut in Settings → Keymap

The script will:
- Only modify documentation blocks containing Doxygen tags
- Preserve all other comments and code
- Skip binary files and non-C++ files
"""

import re
import sys
from pathlib import Path

# Set of all recognized Doxygen tag bases (without @ or \ prefix)
DOXYGEN_TAG_BASES = {
    'brief', 'param', 'return', 'throws', 'exception', 'see', 'note',
    'warning', 'deprecated', 'since', 'author', 'version', 'copyright',
    'pre', 'post', 'invariant', 'tparam', 'typedef', 'defgroup',
    'addtogroup', 'ingroup', 'name', 'enum', 'var', 'fn', 'property'
}

def convert_tags_in_line(line):
    """Convert \tag to @tag in a single line, preserving leading whitespace."""
    leading_ws = line[:len(line) - len(line.lstrip())]
    content = line.lstrip()

    if not content.startswith('///'):
        return line

    for tag_base in DOXYGEN_TAG_BASES:
        content = re.sub(r'\\' + tag_base + r'\b', '@' + tag_base, content)

    return leading_ws + content

def process_file(file_path):
    """Process a single C++ file to convert documentation."""
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except UnicodeDecodeError:
        print(f"Skipping non-text file: {file_path}", file=sys.stderr)
        return

    new_lines = []
    in_block_comment = False
    block_comment_lines = []

    for line in lines:
        stripped = line.strip()

        if stripped.startswith('/**') and not in_block_comment:
            in_block_comment = True
            block_comment_lines = [line]
            continue

        if in_block_comment:
            block_comment_lines.append(line)
            if stripped.endswith('*/'):
                in_block_comment = False
                block_content = ''.join(block_comment_lines)
                if any(re.search(r'[@\\](' + '|'.join(DOXYGEN_TAG_BASES) + r')\b', block_content)):
                    converted_block = convert_block_comment(block_content)
                    new_lines.extend(converted_block.splitlines(keepends=True))
                else:
                    new_lines.extend(block_comment_lines)
            continue

        new_lines.append(convert_tags_in_line(line))

    if new_lines != lines:
        print(f"Updating documentation in: {file_path}")
        with open(file_path, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)

def convert_block_comment(block_content):
    """Convert /** ... */ block comment to /// style."""
    lines = block_content.split('\n')
    converted = []

    # Skip leading/trailing empty lines
    start_idx = 0
    while start_idx < len(lines) and not lines[start_idx].strip():
        start_idx += 1
    end_idx = len(lines) - 1
    while end_idx >= 0 and not lines[end_idx].strip():
        end_idx -= 1

    for line in lines[start_idx:end_idx + 1]:
        stripped = line.strip()
        if stripped.startswith('*'):
            content = line[line.find('*') + 1:].lstrip()
        elif stripped.startswith('/**'):
            content = line[line.find('/**') + 3:].lstrip()
        elif stripped.startswith('*/'):
            continue
        else:
            content = line.lstrip()

        for tag_base in DOXYGEN_TAG_BASES:
            content = re.sub(r'\\' + tag_base + r'\b', '@' + tag_base, content)

        converted.append('/// ' + content if content.strip() else '///')

    return '\n'.join(converted) + '\n'

def find_cpp_files(directory):
    """Recursively find all C++ files in a directory."""
    cpp_extensions = {'.cpp', '.h', '.hpp', '.cxx', '.hxx', '.cc', '.hh'}
    path = Path(directory)
    return [str(p) for p in path.rglob('*') if p.suffix in cpp_extensions]

def main():
    if len(sys.argv) != 2:
        print("Usage: python format_docs.py <file-or-directory>")
        sys.exit(1)

    target = sys.argv[1]
    files = [target] if Path(target).is_file() else find_cpp_files(target)

    for file in files:
        process_file(file)

if __name__ == '__main__':
    main()
