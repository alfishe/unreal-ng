"""python3 -m diskconverter dump <image> | convert <image> <out-image>

The Python counterparts of `zxdisk dump` and `zxdisk convert` (same output, same messages, same exit codes):

    dump     print the floppy as the model holds it, as JSON (identical to `zxdisk dump`)
    convert  load (format probed like FloppyFormats::Probe), save in the format the output extension names
"""

from __future__ import annotations

import sys

from . import formats
from .disk_image import DiskConversionError
from .dump import dump

USAGE = ("usage: python3 -m diskconverter dump <image>\n"
         "       python3 -m diskconverter convert <image> <out-image>\n")


def _load(path: str):
    try:
        return formats.load(path)
    except DiskConversionError as error:
        print(f"zxdisk: {error}", file=sys.stderr)
        return None


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if len(args) == 2 and args[0] == "dump":
        disk = _load(args[1])
        if disk is None:
            return 1
        sys.stdout.write(dump(disk))
        return 0
    if len(args) == 3 and args[0] == "convert":
        disk = _load(args[1])
        if disk is None:
            return 1
        result = formats.save(disk, args[2])
        if not result.saved:
            print(f"zxdisk: {result.reason or 'cannot write ' + args[2]}", file=sys.stderr)
            return 1
        if result.reason:
            print(f"zxdisk: warning: {result.reason}", file=sys.stderr)
        return 0
    sys.stderr.write(USAGE)
    return 2


if __name__ == "__main__":
    sys.exit(main())
