#pragma once

/// @file handheldbrowser.h
/// @brief The handheld menu's file browser model (docs/inprogress/2026-11-12-handheld-menu §4.4): no system file
/// dialog in Game Mode, so the menu lists folders itself. It starts at its roots (recent files, the library folder,
/// the bundled demo software, home, removable cards) and shows the folders and the files the emulator opens:
/// snapshots, tapes, disks. Painting and navigation are the menu's.

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <vector>

class HandheldBrowser
{
public:
    enum class Kind
    {
        Root,
        Folder,
        Up,
        Snapshot,
        Tape,
        Disk,
        File  ///< any other file (a hard disk or CD image for a block slot)
    };

    /// What the browser offers
    enum class Filter
    {
        Any,   ///< every file the emulator opens
        Disk,  ///< disk images (a drive's Insert)
        Tape,  ///< tape images
        All    ///< every file (a block or optical slot's Insert: HDD, CF, SD and CD images)
    };

    struct Entry
    {
        Kind kind = Kind::Folder;
        QString name;
        QString path;    ///< "" for the recent list root
        QString detail;  ///< size and date, or what a root holds
    };

    /// Offering @p filter: in the folder the last file was opened from (its file focused; Up climbs to the roots), at
    /// the roots when there is none or it is gone
    void Open(Filter filter);
    Filter filter() const { return _filter; }

    bool atRoots() const { return _atRoots; }
    /// The folder shown (a root's name at the recent list), "" at the roots
    QString title() const;
    const std::vector<Entry>& entries() const { return _entries; }

    /// Enter the folder of entry @p index, or answer the file's path ("" when a folder was entered)
    QString Activate(int index);
    /// One level up; false at the roots (the browser closes)
    bool Up();
    /// Where the focus goes after a move: the folder the browser came out of, else 0
    int focusAfterMove() const { return _focusAfterMove; }

    /// The kind of a file by its extension; Root when the emulator does not open it
    static Kind KindOf(const QString& path);
    /// The library folder (~/Documents/UnrealNG, with Games/ and Saves/), created on first use
    static QString Library();
    /// Where the menu saves snapshots (Library/Saves)
    static QString Saves();
    /// Remember @p path at the top of the recent files (kept in the settings, 20 at most)
    static void AddRecent(const QString& path);
    static QStringList Recent();

private:
    void listRoots();
    void listRecent();
    void listFolder(const QString& folder);
    bool accepts(Kind kind) const;

    Filter _filter = Filter::Any;
    bool _atRoots = true;
    bool _atRecent = false;
    QString _folder;
    /// The roots the browser went down from: Up past it goes back to the roots
    QString _root;
    std::vector<Entry> _entries;
    int _focusAfterMove = 0;
};
