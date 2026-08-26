# Debugger Enhancement Proposal

Analysis of xpeccy-plus debugger features and proposed enhancements for unreal-qt.

## Priority Focus

**Basic widget parity and usability first**, advanced features (conditional breakpoints, expressions) later.

## Document Index

| Document | Priority | Description |
|----------|----------|-------------|
| [Basic Features Parity](parity-basic-features.md) | **HIGH** | Stack, navigation, flags, signals |
| [Feature Comparison](feature-comparison.md) | Reference | Full xpeccy vs unreal-qt analysis |
| [Expression Evaluator](expression-evaluator.md) | Low | Future: conditional breakpoint support |
| [Breakpoint Enhancements](breakpoint-enhancements.md) | Low | Future: ranges, conditions, hit counts |
| [UI Mockups](mockups.md) | Reference | All widget mockups |

## High Priority (Parity)

| Feature | xpeccy-plus | unreal-qt | Status |
|---------|-------------|-----------|--------|
| Stack depth (9 entries) | SP-2 to SP+E | 4 entries | **TODO** |
| Address history (back/forward) | Yes | None | **TODO** |
| Marked addresses (1-5) | Ctrl+1-5 / Alt+1-5 | None | **TODO** |
| Go to PC hotkey | Yes | None | **TODO** |
| Follow operand address | Enter on JP/CALL | None | **TODO** |
| Individual flag checkboxes | Clickable | String only | **TODO** |
| IFF1/IFF2 + ISR address | Yes | IM only | **TODO** |
| Signal indicators | DOS/ROM/INT | None | **TODO** |
| Port watch | Configurable list | None | **TODO** |

## Low Priority (Future)

| Feature | Description |
|---------|-------------|
| Expression evaluator | C-like language for conditions |
| Conditional breakpoints | Break when expression is true |
| Address range breakpoints | Single range instead of many points |
| Hit counters | Break after N hits |
| IRQ breakpoints | Break on interrupt |
| Heat map | Execution frequency overlay |

## Implementation Phases

### Phase 1: Disassembly Navigation (~2-3 days)
- Address history with back/forward (`Backspace`/`Alt+←`)
- Go to PC hotkey (`Home`)
- Follow operand (`Enter` on JP/CALL/JR)
- Marked addresses (`Ctrl+1-5`, `Alt+1-5`)

### Phase 2: Stack Widget (~1 day)
- Increase to 9 entries (SP-2 to SP+E)
- Single-click jumps to disassembly
- Visual hints for return addresses

### Phase 3: Flags/Registers/Interrupts (~2 days)
- Individual flag checkboxes (toggle S/Z/H/P/N/C)
- IFF1/IFF2 display with ISR address (0x0038 for IM1, resolved for IM2)
- Clickable ISR address → jump to disassembly
- Hover popup with cached ISR disassembly (up to 20 instructions)

### Phase 4: System Status (~2-3 days)
- Memory slots: clickable → jump to memory view / disassembly
- Signal indicators (DOS/ROM/INT)
- Frame counter + T-states
- Beam position with area detection (Screen/Border/VBlank)

### Phase 5: Port Watch (~3-4 days)
- PortRegistry with per-machine port definitions + weak decode masks
- Show all model ports by default, user can hide/rearrange
- Value change highlighting
- Custom ports + breakpoint integration

### Cross-cutting: PreferenceManager (~0.5 day)
- Abstract `IPreferenceProvider` interface
- `QtPreferenceProvider` using QSettings (default)
- Future: `JsonPreferenceProvider` for SDL-only/portable mode
- All widget preferences go through this abstraction

---

**Total for parity: ~10-14 days**

---

## Future: Advanced Features (~10-15 days)

After parity is achieved:
1. Expression evaluator (3-4 days)
2. Conditional breakpoints (2-3 days)
3. Address ranges, hit counters (2-3 days)
4. Heat map, memory map (3-5 days)
