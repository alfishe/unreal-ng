#pragma once

/// @file metadatasettings.h
/// @brief How the Qt debugger uses what is known about the program (the memory markup: the software profile, the
/// analysis, the user's marks) and the labels: global switches of the application (Debug > Metadata, the GUI control
/// plane's metadata.set), kept in the settings. Every debugger window follows them.
///
/// `Metadata` is the main one: off, the debugger shows what it showed before the markup (no data rows, colours or
/// notes) whatever the others say; the labels stay apart.

#include <array>
#include <atomic>

#include <QObject>

class MetadataSettings : public QObject
{
    Q_OBJECT

public:
    enum class Switch
    {
        Metadata,  ///< the markup at all
        Regions,   ///< the disassembly's data rows and instruction starts
        Colours,   ///< the memory view coloured by kind, its legend
        Notes,     ///< the block descriptions and comments in the disassembly and the tooltips
        Labels,    ///< the labels in the disassembly (not markup: apart from Metadata)
    };
    static constexpr size_t kCount = 5;

    static MetadataSettings& Instance();

    /// The switch as set
    bool IsOn(Switch which) const;
    /// What the views use: the switch, and Metadata for the markup ones (any thread)
    bool Uses(Switch which) const;
    /// Set and kept; `changed` (on the GUI thread) and the control surfaces' readouts when it differs
    void Set(Switch which, bool on);
    void Toggle(Switch which) { Set(which, !IsOn(which)); }

signals:
    void changed();

private:
    MetadataSettings();

    std::array<std::atomic<bool>, kCount> _on;
};
