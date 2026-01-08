# Unreal Video Wall

Qt application for displaying multiple ZX Spectrum emulator instances in a video wall grid layout.

## Features

- Multiple emulator instances displayed as tiles (256x192 pixels each)
- Auto-arranging grid layout
- 4K display support (15×11 = 165 tiles)
- Keyboard focus per tile
- Drag-and-drop file loading
- Preset configurations

## Building

```bash
cd unreal-ng
mkdir -p unreal-videowall/cmake-build-debug
cd unreal-videowall/cmake-build-debug
cmake .. -G Ninja
ninja
```

## Running

```bash
./bin/unreal-videowall
```

## Controls

- `Ctrl+N` - Add new emulator tile
- Click tile to focus
- Drag and drop `.sna` or `.trd` files onto tiles

## Development Status

- [x] Phase 1: Basic structure
- [ ] Phase 2: Emulator integration
- [ ] Phase 3: Tile rendering
- [ ] Phase 4: Layout & window modes
- [ ] Phase 5: Interaction
- [ ] Phase 6: Presets & polish
