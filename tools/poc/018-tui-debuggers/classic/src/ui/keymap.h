// keymap.h - keyboard navigation, widget focus traversal and in-place editing.
#pragma once

#include <cstdint>
#include <string>

#include "backend/debugger-backend.h"
#include "panels/paint.h"

namespace dbg {

enum class NavKey {
    Up,
    Down,
    Left,
    Right,
    PgUp,
    PgDn
};

struct RegFieldLayout {
    IDebuggerBackend::RegField regField;
    int width; // 8, 16, 1, 2, or 30..37 (flags)
    int relX;
    int relY;
    int lf, rt, up, dn;
};

extern const RegFieldLayout kRegLayout[26];
extern const char* const kTsControlNames[16];

// Tab traversal across all widgets (forward / reverse)
void FocusNextWidget(UiState& ui, bool isTsconf = false);
void FocusPrevWidget(UiState& ui, bool isTsconf = false);

// Field navigation inside the active widget
void HandleArrowKey(IDebuggerBackend& be, UiState& ui, NavKey key, bool isTsconf = false);

// Character typing / in-place editing
bool HandleCharInput(IDebuggerBackend& be, UiState& ui, char ch);

// Enter key: validation and commit
bool HandleEnterKey(IDebuggerBackend& be, UiState& ui);

// Backspace key: editing buffer
bool HandleBackspaceKey(UiState& ui);

// Escape key: cancel editing
bool HandleEscapeKey(UiState& ui);

// Space key: toggle/cycle
bool HandleSpaceKey(IDebuggerBackend& be, UiState& ui);

// Mouse click: widget activation and field selection
bool HandleMouseClick(IDebuggerBackend& be, UiState& ui, int cellX, int cellY, bool isTsconf = false);

}  // namespace dbg
