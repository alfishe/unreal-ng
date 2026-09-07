# Hobeta — Single TR-DOS File

## Overview

Single TR-DOS file with a 17-byte header. Used to transfer individual files between systems.

- **Extensions**: `.$C`, `.$B`, `.$D`, `.$#`, or any `.$?` pattern
- **Loader**: `core/src/loaders/disk/loader_hobeta.cpp`

## File Layout

```
Header (17 bytes):
  0x00  8   Filename (space-padded)
  0x08  1   Extension/type (B=BASIC, C=CODE, D=DATA, #=screen)
  0x09  2   Start address (little-endian)
  0x0B  2   Length (little-endian)
  0x0D  1   Reserved
  0x0E  1   Sector count
  0x0F  2   Checksum

Data:
  File data (sector_count × 256 bytes, zero-padded)
```

## Checksum

Sum of bytes 0–14 as 16-bit unsigned, stored little-endian at offset 0x0F.

## Detection

Hobeta files have no magic signature. Detection uses:
1. Extension matches `.$?` pattern
2. Header checksum validates
3. File size = 17 + (sector_count × 256)

## Loading (Inject)

Injects the file into an existing disk image or creates a new TR-DOS disk:
1. If no disk loaded: create blank DS80 TR-DOS disk
2. Parse TR-DOS catalog on track 0
3. Find first free entry
4. Write directory entry and file data
5. Update volume sector (file count, free sectors)

## Save (Export)

Extracts a single file from the TR-DOS catalog:
1. Locate file by name/extension
2. Build Hobeta header from catalog entry
3. Export header + data

## Mapping to Model

- Creates/modifies TR-DOS disk with standard 16×256 geometry
- File inserted at first free catalog position
- CRCs regenerated after modification
