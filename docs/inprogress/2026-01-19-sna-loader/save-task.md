# SNA Snapshot Save Implementation

## Phase 1: Core Save Implementation (TDD)
- [ ] Write unit tests for `LoaderSNA::save()` method (48K mode)
- [ ] Write unit tests for `LoaderSNA::save()` method (128K mode)
- [ ] Write unit tests for format auto-selection (48K vs 128K)
- [ ] Implement `LoaderSNA::save()` in [loader_sna.cpp](core/src/loaders/snapshot/loader_sna.cpp)
- [ ] Implement helper methods: `save48k()`, `save128k()`, `saveToStaging()`, `captureStateToStaging()`
- [ ] Add thread-safety with emulator pause/resume during save
- [ ] Verify all save tests pass

## Phase 2: Emulator Integration
- [ ] Add `SaveSnapshot()` method to [Emulator](core/src/emulator/emulator.h#106-107) class
- [ ] Test `SaveSnapshot()` integration with LoaderSNA

## Phase 3: CLI Automation
- [ ] Document `snapshot save` command in [command-interface.md](docs/emulator/design/control-interfaces/command-interface.md)
- [ ] Implement `HandleSnapshotSave()` in [cli-processor-snapshot.cpp](core/automation/cli/src/commands/cli-processor-snapshot.cpp)
- [ ] Add help text for new subcommand
- [ ] Test CLI save functionality

## Phase 4: WebAPI Automation
- [ ] Add `POST /api/v1/emulator/:id/snapshot/save` endpoint
- [ ] Update [openapi_spec.cpp](core/automation/webapi/src/openapi_spec.cpp) with new endpoint
- [ ] Update OpenAPI documentation
- [ ] Test WebAPI save functionality

## Phase 5: Python/Lua Bindings
- [ ] Add `save_snapshot()` to Python emulator bindings
- [ ] Add `save_snapshot()` to Lua emulator bindings
- [ ] Test Python/Lua bindings

## Phase 6: Qt UI Integration
- [ ] Convert "Save Snapshot" menu to submenu with .sna/.z80 options
- [ ] Implement file save dialog with QFileDialog
- [ ] Implement folder persistence via QSettings
- [ ] Implement overwrite confirmation dialog
- [ ] Enable .sna option, disable .z80 option
- [ ] Test Qt save functionality

## Phase 7: Verification & Documentation
- [ ] Run full test suite
- [ ] Manual verification in unreal-qt
- [ ] Update walkthrough documentation
