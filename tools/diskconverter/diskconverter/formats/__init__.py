"""Format registry: extension/magic -> read/write module.

Each module exposes `detect(data: bytes) -> bool`, `read(path) -> DiskImage`, and
`write(disk, path) -> None`. `write` may raise `DiskConversionError` when the target
format structurally cannot hold what's in the disk (e.g. TRD asked to store a
non-standard track) - callers should let that propagate as a hard error, not a warning.
Format modules print their own `warning: ...` lines to stderr for lossy-but-possible
conversions (dropped description text, dropped weak bits, etc.).
"""

from __future__ import annotations

from . import fdi, scl, td0, trd, udi

_BY_EXTENSION = {
    ".fdi": fdi,
    ".scl": scl,
    ".trd": trd,
    ".udi": udi,
    ".td0": td0,
}

_MAGIC_DETECTORS = [fdi, udi, scl, td0]  # trd has no magic - it's a bare sector dump


def module_for_extension(path: str):
    for ext, module in _BY_EXTENSION.items():
        if path.lower().endswith(ext):
            return module
    return None


def module_for_content(path: str):
    with open(path, "rb") as f:
        head = f.read(16)
    for module in _MAGIC_DETECTORS:
        if module.detect(head):
            return module
    return None


def by_name(name: str):
    module = _BY_EXTENSION.get(f".{name.lower().lstrip('.')}")
    if module is None:
        raise ValueError(f"unknown format '{name}' (known: {', '.join(e.lstrip('.') for e in _BY_EXTENSION)})")
    return module


def resolve_input(path: str, explicit: str | None = None):
    """explicit: a format name ("fdi", "scl", "trd", "udi") overriding
    extension/content sniffing. For an existing file, tries extension first (cheap, and
    right for TRD which has no magic), then falls back to sniffing the actual bytes -
    useful when a file was renamed/has a wrong extension."""
    if explicit:
        return by_name(explicit)

    module = module_for_extension(path)
    if module is not None:
        return module

    try:
        module = module_for_content(path)
    except OSError:
        module = None
    if module is not None:
        return module

    raise ValueError(f"cannot determine disk image format for '{path}' - pass --from explicitly")


def resolve_output(path: str, explicit: str | None = None):
    """For a file that does not exist yet, only extension/--to make sense - there is
    nothing to sniff."""
    if explicit:
        return by_name(explicit)
    module = module_for_extension(path)
    if module is not None:
        return module
    raise ValueError(f"cannot determine disk image format for '{path}' - pass --to explicitly")
