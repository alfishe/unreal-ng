"""Format registry: probing, loading and saving the way unreal-disk's FloppyFormats does it.

Each format module exposes `detect(data) -> bool`, `parse(data) -> DiskImage`, `read(path) -> DiskImage`,
`serialize(disk) -> (bytes, warnings)` and `write(disk, path) -> warnings`. Readers set `disk.format` and
`disk.warnings` (the loader's report); readers and writers raise `DiskConversionError(message, warnings)` where the
C++ loader refuses.

- `probe(path)` / `probe_head(data, size, ext)`: FloppyFormats::ProbeHead, the same order and rules, so a file is
  named the format `zxdisk` names it (every format unreal-disk reads).
- `load(path)`: FloppyFormats::Load - probe, then the loader; the failure message is the loader's last warning or
  "not a valid X image", an unknown file "unsupported disk image '...' (supported: ...)".
- `save(disk, path)`: FloppyFormats::Save without retargeting - the writer chosen by the extension; the result's
  reason is the writer's first warning (as `zxdisk convert` prints it).
- `resolve_input` / `resolve_output` / `by_name`: the module picked by an explicit name, the extension or content.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field

from ..disk_image import MAX_CYLINDERS, DiskConversionError, DiskImage
from . import dsk, fdi, hobeta, mb02, mdos, mgt, pro, rawpc, scl, td0, trd, udi
from .fluximage import hfe_format, scp_format

_BY_EXTENSION = {
    ".fdi": fdi,
    ".scl": scl,
    ".trd": trd,
    ".udi": udi,
    ".td0": td0,
}

_MAGIC_DETECTORS = [fdi, udi, scl, td0]  # trd has no magic - it's a bare sector dump

# FloppyFormats::Extensions, for the "unsupported disk image" message
KNOWN_EXTENSIONS = ("trd", "scl", "fdi", "udi", "dsk", "td0", "mgt", "img", "ima", "mb2",
                    "mbd", "d40", "d80", "pro", "hfe", "scp", "$b", "$c", "$d", "$#")

PROBE_BYTES = 17 + 255 * 256  # the largest Hobeta file
TRDOS_ID_OFFSET = 8 * 256 + 0xE7
MAX_TRD_BYTES = 2 * 86 * 16 * 256

# Format name (as ProbeHead returns it) -> (module, title used in "not a valid <title> image")
_LOADERS = {
    "trd": (trd, "TRD"),
    "scl": (scl, "SCL"),
    "udi": (udi, "UDI"),
    "fdi": (fdi, "FDI"),
    "td0": (td0, "TD0"),
    "hobeta": (hobeta, "Hobeta"),
    "mb02": (mb02, "MB-02"),
    "mdos": (mdos, "MDOS"),
    "dsk": (dsk, "DSK"),
    "mgt": (mgt, "MGT"),
    "rawpc": (rawpc, "raw PC floppy"),
    "pro": (pro, "PRO"),
    "hfe": (hfe_format, "HFE"),
    "scp": (scp_format, "SCP"),
}
_TITLES = {"dsk": "DSK", "mgt": "MGT", "rawpc": "raw PC floppy", "pro": "PRO", "mb02": "MB-02", "mdos": "MDOS",
           "hobeta": "Hobeta", "hfe": "HFE", "scp": "SCP"}

# Save: extension -> module; extensions unreal-disk writes with loaders this package does not port
_SAVERS = {"udi": udi, "fdi": fdi, "td0": td0, "scl": scl, "mb2": mb02, "mbd": mb02, "d40": mdos, "d80": mdos,
           "dsk": dsk, "ima": rawpc, "pro": pro, "hfe": hfe_format, "scp": scp_format}
_UNPORTED_SAVERS: dict[str, str] = {}


def extension(path: str) -> str:
    """FileHelper::GetFileExtension, lower case: the text after the last dot of the last path component"""
    name = path.replace("\\", "/").rsplit("/", 1)[-1]
    if name in (".", ".."):
        return ""
    dot = name.rfind(".")
    if dot <= 0:
        return ""
    return name[dot + 1:].lower()


def is_hobeta_extension(ext: str) -> bool:
    return len(ext) == 2 and ext[0] == "$"


def _hobeta_detect(data: bytes) -> bool:
    return hobeta.detect(data)


def _mdos_detect(data: bytes, size: int) -> bool:
    return mdos.detect(data, size)


def _mb02_detect(data: bytes, size: int) -> bool:
    return mb02.detect(data, size)


def probe_head(data: bytes, size: int, ext: str) -> str:
    """FloppyFormats::ProbeHead: data is the first min(size, PROBE_BYTES) bytes of the file"""
    length = len(data)
    if data[:4] in (b"UDI!", b"udi!"):
        return "udi"
    if data[:3] == b"FDI":
        return "fdi"
    if length >= 8 and data[:8] in (b"MV - CPC", b"EXTENDED"):
        return "dsk"
    if data[:3] == b"SCP":
        return "scp"
    if length >= 8 and data[:8] in (b"HXCPICFE", b"HXCHFEV3"):
        return "hfe"
    if length >= 8 and data[:8] == b"SINCLAIR":
        return "scl"
    if length == size and _hobeta_detect(data):
        return "hobeta"
    if td0.detect(data) and ext in ("td0", ""):
        return "td0"
    if _mdos_detect(data, size):
        return "mdos"
    if size in (737280, 1474560):
        return "rawpc"
    if ext == "pro" and size % 10240 == 0 and 80 <= size // 10240 <= 83:
        return "pro"
    if _mb02_detect(data, size):
        return "mb02"
    if size % 256 == 0 and size <= MAX_TRD_BYTES and length > TRDOS_ID_OFFSET and data[TRDOS_ID_OFFSET] == 0x10:
        return "trd"
    if size in (819200, 409600) and ext in ("mgt", "img"):
        return "mgt"
    if ext == "trd" and size % 256 == 0 and 0 < size <= MAX_TRD_BYTES:
        return "trd"
    if ext == "td0" and td0.detect(data):
        return "td0"
    return ""


def probe(path: str) -> str:
    """FloppyFormats::Probe"""
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        head = f.read(min(size, PROBE_BYTES))
    return probe_head(head, size, extension(path))


def load(path: str) -> DiskImage:
    """FloppyFormats::Load: probe, then the format's loader. Raises DiskConversionError with the message zxdisk
    prints (the loader's last warning, or "not a valid X image") and the loader's warnings."""
    try:
        fmt = probe(path)
    except OSError:
        fmt = ""
    if not fmt:
        ext = extension(path)
        if is_hobeta_extension(ext):
            raise DiskConversionError(f"'{path}' has no valid Hobeta header")
        known = " ".join("." + e for e in KNOWN_EXTENSIONS)
        raise DiskConversionError(f"unsupported disk image '{path}' (supported: {known})")
    entry = _LOADERS.get(fmt)
    if entry is None:
        raise DiskConversionError(f"{_TITLES.get(fmt, fmt)} images ('{fmt}') are not read by diskconverter "
                                  f"(zxdisk reads them)")
    module, title = entry
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        raise DiskConversionError(f"not a valid {title} image")
    if not data:  # the loaders' loadImage: TRD / SCL fail silently, the others say why
        if module in (trd, scl):
            raise DiskConversionError(f"not a valid {title} image")
        message = "File is empty: " + path
        raise DiskConversionError(message, [message])
    try:
        # an MGT dump's track order comes from its extension (.img side-major, .mgt cylinder-major)
        disk = module.parse(data, mgt.order_for_extension(path)) if module is mgt else module.parse(data)
    except DiskConversionError as error:
        message = error.warnings[-1] if error.warnings else f"not a valid {title} image"
        raise DiskConversionError(message, error.warnings) from None
    disk.format = fmt
    return disk


@dataclass
class SaveResult:
    """FloppySaveResult without retargeting: saved, the reason zxdisk prints, all writer warnings"""

    saved: bool
    reason: str = ""
    warnings: list[str] = field(default_factory=list)


def serialize(disk: DiskImage, path: str) -> tuple[bytes, list[str]]:
    """The writer FloppyFormats::Save picks for the path's extension, without writing a file. Raises
    DiskConversionError (message = the reason zxdisk prints) when the writer refuses."""
    ext = extension(path)
    if is_hobeta_extension(ext):
        raise DiskConversionError("a Hobeta file holds one TR-DOS file, not a disk: save the disk as .trd, .scl or "
                                  ".udi")
    if ext in _UNPORTED_SAVERS:
        raise DiskConversionError(f"saving {_UNPORTED_SAVERS[ext]} (.{ext}) is not supported by diskconverter")
    module = _SAVERS.get(ext, trd)  # anything else is written as TRD, as WriteByExtension does
    # .img is shared: an MGT (+D, 10 x 512) disk stays MGT (side-major), anything else is a raw PC floppy
    if ext == "mgt" or (ext == "img" and mgt.is_plus_d_geometry(disk)):
        module = mgt
    elif ext == "img":
        module = rawpc
    try:
        return mgt.serialize(disk, mgt.order_for_extension(path)) if module is mgt else module.serialize(disk)
    except DiskConversionError as error:
        reason = error.warnings[0] if error.warnings else "save failed"
        raise DiskConversionError(reason, error.warnings) from None


def save(disk: DiskImage, path: str) -> SaveResult:
    """FloppyFormats::Save(allowRetarget = false)"""
    try:
        data, warnings = serialize(disk, path)
    except DiskConversionError as error:
        return SaveResult(False, str(error), error.warnings)
    with open(path, "wb") as f:
        f.write(data)
    return SaveResult(True, warnings[0] if warnings else "", warnings)


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
    """explicit: a format name ("fdi", "scl", "trd", "udi", "td0") overriding extension/content sniffing. For an
    existing file, tries extension first (cheap, and right for TRD which has no magic), then falls back to sniffing
    the actual bytes - useful when a file was renamed/has a wrong extension."""
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
    """For a file that does not exist yet, only extension/--to make sense - there is nothing to sniff."""
    if explicit:
        return by_name(explicit)
    module = module_for_extension(path)
    if module is not None:
        return module
    raise ValueError(f"cannot determine disk image format for '{path}' - pass --to explicitly")
