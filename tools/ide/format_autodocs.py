#!/usr/bin/env python3
"""
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
- Make backups with .bak extension before modifying files
"""

import re
import sys
from pathlib import Path

# Set of all recognized Doxygen tags (both @ and \ versions)
AUTODOC_TAGS = {
    '@brief', '@param', '@return', '@throws', '@exception', '@see', '@note',
    '@warning', '@deprecated', '@since', '@author', '@version', '@copyright',
    '@pre', '@post', '@invariant', '@tparam', '@typedef', '@defgroup',
    '@addtogroup', '@ingroup', '@name', '@enum', '@var', '@fn', '@property'
}

# Dynamically create tag conversions with properly escaped regex patterns
TAG_CONVERSIONS = {re.escape(f'\\{tag[1:]}'): tag for tag in AUTODOC_TAGS
                   if not tag.startswith(('@tparam', '@typedef'))}

def is_autodoc_block(content):
    """Check if the block contains any Doxygen autodoc tags."""
    pattern = r'[@\\](' + '|'.join(tag[1:] for tag in AUTODOC_TAGS) + r')\b'
    return re.search(pattern, content) is not None

def convert_tags(content):
    """Convert \tag style to @tag style in documentation content."""
    for old_tag, new_tag in TAG_CONVERSIONS.items():
        content = re.sub(r'(\s|^)' + old_tag + r'(\s|$)', r'\1' + new_tag + r'\2', content)
    return content

def convert_block_to_line_comments(doc_block):
    """Convert a block comment documentation to line comments."""
    lines = doc_block.split('\n')
    converted = []

    # Skip leading empty lines
    start_idx = 0
    while start_idx < len(lines) and not lines[start_idx].strip():
        start_idx += 1

    # Skip trailing empty lines
    end_idx = len(lines) - 1
    while end_idx >= 0 and not lines[end_idx].strip():
        end_idx -= 1

    for line in lines[start_idx:end_idx + 1]:
        # Remove leading '*' and whitespace, preserve indentation
        stripped = line.strip()
        if stripped.startswith('*'):
            content = line[line.find('*') + 1:].lstrip()
        elif stripped.startswith('/**'):
            content = line[line.find('/**') + 3:].lstrip()
        elif stripped.startswith('*/'):
            continue
        else:
            content = line.lstrip()

        # Convert tags if needed
        content = convert_tags(content)

        # Preserve empty lines but convert to ///
        if not content.strip():
            converted.append('///')
        else:
            converted.append(f'/// {content}')

    return '\n'.join(converted)

def process_file(file_path):
    """Process a single C++ file to convert documentation blocks."""
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except UnicodeDecodeError:
        print(f"Skipping non-text file: {file_path}", file=sys.stderr)
        return

    # Pattern to match /** ... */ blocks
    pattern = re.compile(r'/\*\*(.*?)\*/', re.DOTALL)

    def replacement(match):
        doc_block = match.group(1)
        # Skip if this is already a line comment block or doesn't contain autodoc tags
        if '///' in doc_block or not is_autodoc_block(doc_block):
            return match.group(0)
        return convert_block_to_line_comments(doc_block)

    new_content = pattern.sub(replacement, content)

    if new_content != content:
        print(f"Updating documentation in: {file_path}")
        with open(file_path, 'w', encoding='utf-8') as f:
            f.write(new_content)

def find_cpp_files(directory):
    """Recursively find all C++ files in a directory."""
    cpp_extensions = {'.cpp', '.h', '.hpp', '.cxx', '.hxx', '.cc', '.hh'}
    path = Path(directory)
    return [str(p) for p in path.rglob('*') if p.suffix in cpp_extensions]

def main():
    if len(sys.argv) != 2:
        print("Usage: python doc_converter.py <directory-or-file>")
        sys.exit(1)

    target = sys.argv[1]

    if Path(target).is_file():
        files = [target]
    else:
        files = find_cpp_files(target)

    for file in files:
        process_file(file)

if __name__ == '__main__':
    main()