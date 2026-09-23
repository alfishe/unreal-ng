"""Format-agnostic in-memory disk model shared by every reader/writer.

Mirrors the C++ core's `DiskImage`/`Track`/`Sector` model (core/src/emulator/io/fdc/diskimage.h)
closely enough that a track dumped from one format and written to another round-trips its
sector content exactly, for the parts every format actually agrees on (which C/H/R/N a
sector has, whether its data CRC is valid, whether it's deleted, and its bytes).

What is deliberately NOT modeled here (because no supported format needs it for conversion):
gap lengths, clock-mark bitmaps, weak/flaky bits, raw MFM byte streams. FDI/TRD/SCL are all
lossy for that information already; UDI is the only format below that could carry gaps/clock
marks, and this tool treats UDI purely as a sector container (see formats/udi.py) since the
other formats it needs to interoperate with have no place to put that data anyway.
"""

from __future__ import annotations

from dataclasses import dataclass, field


class DiskConversionError(Exception):
    """Raised when a conversion cannot be performed at all (not just degraded)."""


@dataclass
class Sector:
    """One physical sector, as addressed by its own ID field (which may not match its
    physical track/head position - some copy-protected disks rely on exactly that)."""

    cylinder: int
    head: int
    number: int
    size_code: int  # 0=128B 1=256B 2=512B 3=1024B (128 << size_code)
    data: bytes  # length == data_size, or 0 bytes when no_data is set
    crc_valid: bool = True
    deleted: bool = False
    no_data: bool = False

    @property
    def data_size(self) -> int:
        return 128 << (self.size_code & 3)

    def __post_init__(self) -> None:
        if not self.no_data and len(self.data) != self.data_size:
            raise DiskConversionError(
                f"sector C={self.cylinder} H={self.head} R={self.number}: "
                f"data length {len(self.data)} does not match size code {self.size_code} "
                f"({self.data_size} bytes)"
            )


@dataclass
class Track:
    """Sectors in physical (stream) order, as they would be encountered by a head sweeping
    the track once. Order matters for interleave-sensitive consumers; conversion here does
    not need real timing, so "physical order" is just "the order the source format listed
    them in"."""

    sectors: list[Sector] = field(default_factory=list)

    def find(self, number: int) -> Sector | None:
        for sector in self.sectors:
            if sector.number == number:
                return sector
        return None

    def is_standard_trdos(self) -> bool:
        """16 sectors, R=1..16 (any physical order), 256 bytes, data present and CRC-valid,
        not deleted - the only geometry TRD (and a TR-DOS-compatible SCL round-trip) can
        express."""
        if len(self.sectors) != 16:
            return False
        seen = set()
        for sector in self.sectors:
            if (
                sector.size_code != 1
                or sector.no_data
                or sector.deleted
                or not sector.crc_valid
                or not (1 <= sector.number <= 16)
            ):
                return False
            seen.add(sector.number)
        return seen == set(range(1, 17))


@dataclass
class DiskImage:
    cylinders: int
    heads: int
    write_protected: bool = False
    tracks: dict[tuple[int, int], Track] = field(default_factory=dict)

    def track(self, cylinder: int, head: int) -> Track:
        return self.tracks.setdefault((cylinder, head), Track())

    def get_track(self, cylinder: int, head: int) -> Track | None:
        return self.tracks.get((cylinder, head))

    def is_standard_trdos_geometry(self) -> bool:
        """Whether every track in the declared geometry is a plain 16x256 TR-DOS track -
        the geometry TRD requires and SCL's file-level round-trip depends on."""
        if self.heads not in (1, 2):
            return False
        for cyl in range(self.cylinders):
            for head in range(self.heads):
                trk = self.get_track(cyl, head)
                if trk is None or not trk.is_standard_trdos():
                    return False
        return True

    def sector_bytes(self, cylinder: int, head: int, number: int) -> bytes | None:
        trk = self.get_track(cylinder, head)
        if trk is None:
            return None
        sector = trk.find(number)
        if sector is None or sector.no_data:
            return None
        return sector.data
