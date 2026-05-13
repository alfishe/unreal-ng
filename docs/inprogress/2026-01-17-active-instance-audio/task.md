# Audio Binding on Tile Focus

## Status: Planning

## Tasks

- [/] Plan audio binding feature implementation
  - [x] Explore videowall tile and focus handling
  - [x] Understand sound manager and audio callback architecture
  - [x] Identify feature management for sound generation toggle
  - [ ] Write implementation plan
  - [ ] Get user approval
- [ ] Implement audio binding feature
  - [ ] Add SoundManager to VideoWallWindow
  - [ ] Add signals for tile focus change to notify VideoWallWindow
  - [ ] Implement `bindAudioToTile()` method
  - [ ] Update `EmulatorTile::focusInEvent()` to emit focus signal
  - [ ] Handle audio unbinding on [focusOutEvent()](unreal-videowall/src/videowall/EmulatorTile.cpp#104-110)
  - [ ] Enable sound feature temporarily for focused tile
- [ ] Verify implementation
  - [ ] Manual testing of tile focus audio switching
