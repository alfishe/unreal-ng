#pragma once

/// @file tabscrollarrows.h
/// @brief Tab bars that scroll instead of growing: scroll arrows at both ends of a horizontal bar (the back arrow at
/// the left end, the forward one at the right end, each half as wide again as the style's), tab names whole, and the
/// mouse wheel over the bar switching tabs.

class QTabBar;

/// Turns on the bar's scroll buttons and places them at its two ends, their arrows kept small and centered. The style
/// it sets proxies the application style, or the style named `baseStyle` (QStyleFactory, e.g. "Fusion"), and belongs
/// to the bar. Shared with eCat (debuggers/qt): Qt only
void UseEndScrollArrows(QTabBar* bar, const char* baseStyle = nullptr);

/// The wheel over the bar makes the next (down, right) or the previous (up, left) visible enabled tab current, one tab
/// per wheel notch; a touchpad's small steps add up to a notch. The filter belongs to the bar
void UseWheelToSwitchTabs(QTabBar* bar);
