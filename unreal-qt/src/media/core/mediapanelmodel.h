#pragma once

/// @file mediapanelmodel.h
/// @brief Qt-free logic of the media panel: MediaControl replies (StateNode)
/// turned into the rows the panel shows, the file-dialog filter of a slot
/// kind, and whether a refused request needs the save / export / discard
/// question. The window (mediapanelwindow.cpp) only draws these.
/// Design: docs/inprogress/2026-09-28-storage-manager/media-control-design.md §3.9

#include <string>
#include <vector>

#include "emulator/state/statenode.h"

struct MediaPanelRow
{
    std::string slot;      ///< the slot id (fdd.a, sd.zc); a detached medium's former slot
    std::string alias;     ///< first alias ("A", "sd"), may be empty
    std::string kind;      ///< floppy, tape, block, optical
    std::string label;     ///< "Drive A", "SD card (Z-Controller)"
    std::string state;     ///< empty, present, pending, detached
    std::string medium;    ///< source path, empty when the slot is empty
    std::string format;
    std::string access;
    std::string dirty;     ///< "3 tracks" / "48 sectors" / ""
    bool present = false;
    bool detached = false;
    bool isDirty = false;
    bool writeProtect = false;
    bool acceptsFolder = false;
};

/// The rows of a "list" reply: slots first, then detached media
std::vector<MediaPanelRow> MediaPanelRows(const StateNode& listReply);

/// A Qt file-dialog filter for a slot kind from a "formats" reply:
/// "Disk images (*.trd *.scl ...);;All files (*)"
std::string MediaFileFilter(const StateNode& formatsReply, const std::string& kind);

/// The request was refused only because a dirty medium would leave its slot:
/// the panel asks Save / Export / Discard / Cancel and retries with the answer
bool MediaNeedsDisposition(const StateNode& reply);
