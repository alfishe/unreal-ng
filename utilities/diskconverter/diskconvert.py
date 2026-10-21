#!/usr/bin/env python3
"""Convert ZX-Spectrum/TR-DOS disk images between SCL, TRD, FDI and UDI.

    diskconvert.py <input> <output> [--from FORMAT] [--to FORMAT]

Format is guessed from each path's extension (falling back to content sniffing for the
input file) unless overridden with --from/--to. A conversion that would silently lose or
corrupt data (e.g. writing a non-standard-geometry disk to TRD, or an SCL export from a
disk with no TR-DOS catalog) is refused with an error; conversions that are merely lossy
in a way the target format cannot help (e.g. FDI/TRD/SCL dropping weak bits, or SCL only
keeping catalogued files) print a warning and proceed.

Examples:
    diskconvert.py game.scl game.trd
    diskconvert.py game.trd game.fdi
    diskconvert.py game.fdi game.udi
    diskconvert.py dump.udi dump.trd --to trd
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from diskconverter.disk_image import DiskConversionError
from diskconverter import formats


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", help="source disk image")
    parser.add_argument("output", help="destination disk image")
    parser.add_argument("--from", dest="from_format", metavar="FORMAT", help="force input format (scl|trd|fdi|udi)")
    parser.add_argument("--to", dest="to_format", metavar="FORMAT", help="force output format (scl|trd|fdi|udi)")
    args = parser.parse_args(argv)

    try:
        reader = formats.resolve_input(args.input, args.from_format)
        writer = formats.resolve_output(args.output, args.to_format)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    try:
        disk = reader.read(args.input)
    except DiskConversionError as e:
        print(f"error: reading {args.input}: {e}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    try:
        writer.write(disk, args.output)
    except DiskConversionError as e:
        print(f"error: writing {args.output}: {e}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    print(f"{args.input} -> {args.output} ({reader.__name__.rsplit('.', 1)[-1]} -> {writer.__name__.rsplit('.', 1)[-1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
