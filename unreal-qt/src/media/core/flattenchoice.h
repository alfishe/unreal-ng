#pragma once

/// @file flattenchoice.h
/// @brief Qt-free logic of the strategy dialog for a composite's unsaved writes (DT-9, interactive): which
/// strategies a composite can take and why not, the one preselected from the descriptor's `writes.save`, and the
/// `flatten` request options of a choice. The dialog (flattendialog.cpp) only draws these.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c8-commit-writeback.md (C8c).

#include <map>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"
#include "media/core/mediapanelmodel.h"

enum class FlattenStrategy
{
    Flat,       ///< S1: one image at a path; the slot then holds it
    Delta,      ///< S2: the session next to the descriptor, restored at the next insert
    Commit,     ///< S3: everything into the graft's base image
    WriteBack,  ///< S4: the guest's files into the writable folder layers
};

struct FlattenOption
{
    FlattenStrategy strategy = FlattenStrategy::Delta;
    std::string name;     ///< "flat", "delta", "commit", "write-back" (the request's value)
    std::string title;    ///< for the radio button
    std::string help;     ///< what it does
    bool available = true;
    std::string reason;   ///< why not, when not available
};

struct FlattenChoice
{
    FlattenStrategy strategy = FlattenStrategy::Delta;
    std::string path;       ///< Flat: the image to write
    bool compact = false;   ///< Flat: lay the FAT volume out again
    bool keepBoth = false;  ///< WriteBack: a conflict keeps both files
    bool force = false;     ///< Commit / WriteBack: although the guest's file system has lost clusters
};

/// A composite medium in a panel row (its format names the build: compose-*, graft-*)
bool IsCompositeRow(const MediaPanelRow& row);

/// The four strategies for the composite a `layers` reply describes, each with whether it applies and why not
std::vector<FlattenOption> FlattenOptionsFor(const StateNode& layersReply);

/// The strategy the dialog starts on: the descriptor's writes.save when it is available, else delta
FlattenStrategy PreselectedStrategy(const std::vector<FlattenOption>& options, const StateNode& layersReply);

/// The `flatten` request's options for `choice`; `plan` asks for the plan only (commit, write-back)
std::map<std::string, std::string> FlattenRequestOptions(const FlattenChoice& choice, bool plan);

/// The lines of a reply's `report` (the plan, or what was done)
std::vector<std::string> ReplyReport(const StateNode& reply);
