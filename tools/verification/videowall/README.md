# Videowall WebAPI Test Infrastructure

Python test scripts for verifying videowall functionality via WebAPI.

## Setup

```bash
pip install -r requirements.txt
```

## Usage

Run tests from the `src` directory. By default, snapshots are restricted to those listed in `../whitelist.txt`:

```bash
cd src
python test_load_random_snapshots.py --url http://localhost:8090
```

### Custom Snapshot Whitelist

You can specify a custom whitelist file to restrict which snapshots are loaded:

```bash
cd src
python test_load_random_snapshots.py --url http://localhost:8090 --whitelist ../my_whitelist.txt
```

To allow all snapshots (no restriction), create an empty whitelist file or omit the whitelist entirely but ensure the default `../whitelist.txt` contains all desired snapshots.

### Pattern Loading (Dynamic Videowall)

For fullscreen videowall mode, use the pattern loading script. Grid dimensions are calculated dynamically using ceiling division.

#### Grid Configurations

| Resolution | Tile Size | Cols | Rows | Total |
|------------|-----------|------|------|-------|
| 4K (3840×2160) | 256×196 | 15 | 12 | **180** |
| QHD (2560×1600) | 256×196 | 10 | 9 | **90** |
| FullHD (1920×1080) | 256×196 | 8 | 6 | **48** |
| 4K (3840×2160) | 512×384 | 8 | 6 | **48** |
| QHD (2560×1600) | 512×384 | 5 | 5 | **25** |
| FullHD (1920×1080) | 512×384 | 4 | 3 | **12** |

> **Note**: Grid uses ceiling division: `cols = ceil(screenWidth / tileWidth)`, `rows = ceil(screenHeight / tileHeight)`

```bash
cd src
python test_load_pattern_snapshots.py --url http://localhost:8090 --pattern spiral
```

#### Available Patterns

| Pattern | Description |
|---------|-------------|
| `all_same` | All 165 instances load the same snapshot |
| `columns` | Same snapshot per column (11 instances each) |
| `rows` | Same snapshot per row (15 instances each) |
| `diagonal_down` | Diagonal lines (top-right to bottom-left) |
| `diagonal_up` | Diagonal lines (top-left to bottom-right) |
| `circles` | Concentric circles from center outward |
| `spiral` | Spiral pattern from center outward |
| `checkerboard` | Alternating checkerboard pattern |
| `waves_horizontal` | Horizontal wave pattern |
| `waves_vertical` | Vertical wave pattern |

#### Pattern Examples

```bash
# Create a beautiful spiral pattern
python test_load_pattern_snapshots.py --pattern spiral

# Checkerboard effect
python test_load_pattern_snapshots.py --pattern checkerboard

# Concentric circles
python test_load_pattern_snapshots.py --pattern circles

# Diagonal stripes
python test_load_pattern_snapshots.py --pattern diagonal_down
```

### Pattern Cycling

For a continuous visual experience, use the pattern cycling script to automatically cycle through all available patterns:

```bash
cd src
./cycle_patterns.sh [delay_seconds]
```

#### Cycling Examples

```bash
# Cycle through all patterns with default 15-second delay
./cycle_patterns.sh

# Cycle with 30-second delay between patterns
./cycle_patterns.sh 30

# Fast cycling with 5-second transitions
./cycle_patterns.sh 5
```

The cycling script will:
- Load each pattern in sequence
- Display pattern descriptions and progress
- Sleep for the specified delay between patterns
- Loop forever until interrupted with Ctrl+C
- Show cycle count and pattern progress

#### Whitelist File Format

The whitelist file should contain one exact snapshot filename per line, including the file extension (.sna or .z80). Lines starting with `#` are treated as comments and ignored. Empty lines are also ignored.

Example whitelist file (`whitelist.txt` is the default):
```
# Allow specific snapshots - exact filenames required
action.sna
Dizzy X.sna
dizzyx.z80

# This is a comment
7threality.sna
```

Filenames must match exactly, including case and extension. The extension is required.

## Test Scenarios

| Script | Description |
|--------|-------------|
| `test_load_random_snapshots.py` | Load random snapshots to all instances, disable sound, ensure all running. Supports whitelist filtering of allowed snapshots. |
| `test_load_pattern_snapshots.py` | Load snapshots in visual patterns to fullscreen videowall. Grid dimensions detected dynamically. Supports whitelist filtering. |
| `cycle_patterns.sh` | Automatically cycle through all patterns with configurable delay. Requires fullscreen videowall mode. |

## Architecture

- `webapi_client.py` - Reusable WebAPI client with helper methods
  - `WebAPIClient` - HTTP client for emulator management
  - `TestDataHelper` - Access to testdata files with project root auto-detection
