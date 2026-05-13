# Work Item: DiskImage Modernization

> **Date:** 2026-01-24  
> **Status:** Planning (Questions Pending)  
> **Priority:** High (Prerequisite for WD1793 work)  
> **Effort:** 8-12 days estimated  

## Summary

Modernize the DiskImage subsystem to support:
- Variable sector sizes and track layouts
- Write protection
- Change tracking and heatmaps
- DiskManager for centralized disk operations
- Safe save mechanisms
- Format conversion

## Relationship to WD1793 Work

```
┌─────────────────────────────────────────┐
│   DiskImage Modernization (THIS)        │  ← Prerequisite
│   8-12 days                             │
└──────────────────┬──────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────┐
│   WD1793 Full Test Coverage             │  ← Depends on DiskImage
│   15-21 days                            │
└─────────────────────────────────────────┘
```

## Documents

| Document | Description |
|----------|-------------|
| [implementation-plan.md](implementation-plan.md) | Full plan with phases and questions |

## Quick Stats

| Feature | Status |
|---------|--------|
| Write Protection | 📋 Planned |
| Non-Standard Layouts | 📋 Planned |
| Change Tracking | 📋 Planned |
| DiskManager | 📋 Planned |
| Save Mechanisms | 📋 Planned |
| Format Conversion | 📋 Planned |

## Phase Summary

| Phase | Name | Effort | Description |
|-------|------|--------|-------------|
| 1 | Core Data Structures | 2-3 days | SectorDescriptor, TrackGeometry |
| 2 | Write Protection | 1 day | WD1793 integration |
| 3 | Change Tracking | 2-3 days | Dirty flags, heatmaps |
| 4 | DiskManager | 2-3 days | Centralized management |
| 5 | Persistence / Save | 2-3 days | Save modes, confirmation |
| 6 | Format Conversion | 2 days | Multi-format I/O |

## Open Questions (7)

1. **Multi-Instance Sharing** - Exclusive access vs copy-on-write?
2. **Auto-Save Behavior** - When to save automatically?
3. **Heatmap Persistence** - Session-only or stored?
4. **Backup Strategy** - Create .bak files?
5. **SD Card / HDD Management** - What exactly is needed?
6. **Format Detection** - How to handle unknown extensions?
7. **Test Images** - Do we have copy protection samples?

See [implementation-plan.md](implementation-plan.md) for details.

## Related

- [WD1793 Test Coverage](../2026-01-24-wd1793-test-coverage/README.md)
- [WD1793 Documentation](../../WD1793/)
