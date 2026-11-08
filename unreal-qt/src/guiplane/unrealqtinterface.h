#pragma once

/// @file unrealqtinterface.h
/// @brief The declaration of unreal-qt's own commands on the GUI control plane (`unrealqt.*`, design §4.2), without
/// widgets: the program, the GUI catalogue writer and the tests link it

/// Adds the `unrealqt` interface to the plane's declarations (guicontrol::AddProgramInterface); repeated calls replace it
void DeclareUnrealQtInterface();
