"""HFE / SCP as DiskImage formats: the flux modules (`hfe`, `scp`) work on `FluxDisk` track tables; this adapter
gives them the registry interface of the other formats (`parse(data) -> DiskImage`, `serialize(disk)`).

As in C++: a track the flux file does not hold (SCP) keeps the DiskImage default - a TR-DOS formatted track; a save
writes the header fields of a fresh loader (FloppyFormats::Save creates one per call)."""

from __future__ import annotations

from ..disk_image import DiskConversionError, DiskImage, TrackFormatSpec
from ..flux import RawTrackData
from . import hfe, scp


def _to_disk(flux_disk, fmt: str) -> DiskImage:
    disk = DiskImage(flux_disk.cylinders, flux_disk.sides)
    for cylinder in range(flux_disk.cylinders):
        for side in range(flux_disk.sides):
            track = disk.get_track(cylinder, side)
            data: RawTrackData | None = flux_disk.tracks.get((cylinder, side))
            if data is None:
                track.format_track(cylinder, side, TrackFormatSpec.trdos())
            else:
                track.set_stream(data.raw, data.clock, data.weak, data.encoding)
    disk.format = fmt
    disk.warnings = list(flux_disk.warnings)
    return disk


def _to_tracks(disk: DiskImage) -> dict:
    tracks = {}
    for cylinder, side, track in disk.iter_tracks():
        weak = track.weak_bitmap() if track.has_weak_bits() else None
        tracks[(cylinder, side)] = RawTrackData(track.encoding, bytes(track.raw), bytes(track.clock), weak)
    return tracks


class _FluxFormat:
    def __init__(self, module, name: str):
        self._module = module
        self._name = name

    def detect(self, data: bytes) -> bool:
        return bool(self._module.detect(data))

    def parse(self, data: bytes) -> DiskImage:
        try:
            return _to_disk(self._module.parse(data), self._name)
        except ValueError as error:
            raise DiskConversionError(str(error), [str(error)]) from None

    def read(self, path: str) -> DiskImage:
        with open(path, "rb") as handle:
            return self.parse(handle.read())

    def serialize(self, disk: DiskImage) -> tuple[bytes, list[str]]:
        try:
            return self._module.serialize(_to_tracks(disk), disk.cylinders, disk.heads), []
        except ValueError as error:
            raise DiskConversionError(str(error), [str(error)]) from None

    def write(self, disk: DiskImage, path: str) -> list[str]:
        data, warnings = self.serialize(disk)
        with open(path, "wb") as handle:
            handle.write(data)
        return warnings


hfe_format = _FluxFormat(hfe, "hfe")
scp_format = _FluxFormat(scp, "scp")
