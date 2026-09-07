/**
 * @file tapeblockdialog.h
 * @brief TapeBlockDialog — block-content popup for the Tape Manager (r7).
 *
 * BASIC program data blocks decode through the core BasicExtractor; every
 * other byte-payload block (headers included) shows as a QHexView hex dump.
 * Pulse-stream and control entries carry no bytes — an info line says so.
 *
 * All inputs are copies: the dialog is pure display, it never touches Tape*
 * or EmulatorContext.
 */

#pragma once

#include <QDialog>

#include <vector>

#include "emulator/io/tape/tapetypes.h"  // TapeBlockDescriptor

class TapeBlockDialog : public QDialog
{
    Q_OBJECT

public:
    /// `payload` is the block's raw data copy (flag + body + checksum for
    /// framed blocks, empty for pulse/control entries); `pairedHeader` may be
    /// null (unpaired or non-data block) and is copied when present.
    explicit TapeBlockDialog(const TapeBlockDescriptor& descriptor,
                             const TapeBlockDescriptor* pairedHeader,
                             const std::vector<uint8_t>& payload,
                             QWidget* parent = nullptr);
};
