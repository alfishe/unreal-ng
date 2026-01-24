# WD1793 Write Track Debugging

## Tasks
- [/] Analyze unrealspeccy reference implementation
- [ ] Create IWD1793Observer test hook for WRITE_TRACK capture
- [ ] Write unit test that:
  - [ ] Issues WRITE_TRACK command manually
  - [ ] Writes known MFM pattern via Data Register
  - [ ] Validates raw track buffer after completion
  - [ ] Validates reindexed sector structure
- [ ] Integrate into FORMAT integration test
- [ ] Verify all 160 tracks formatted correctly
