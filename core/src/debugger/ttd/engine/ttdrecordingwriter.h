#pragma once

/// @file ttdrecordingwriter.h
/// @brief A recording on disk as segment files (D41, phase-4 TDD §5.2.7,
/// §5.3): one file per segment of the engine's history in the recording's
/// folder, written as it records; the files of a recording joined into one
/// .ttd ("save as"); a recording loaded back, the last window by default.
///
/// Worked example: segments of a minute, a ring of 5 minutes. After 12
/// minutes the folder holds segment-0000.ttd ... segment-0011.ttd (the whole
/// recording) while memory holds the last 5-6 minutes. LoadRecording with a
/// 5-minute window reads the last six files; with 0 it reads all twelve.
/// JoinSessionFiles copies their records into one file without decoding them.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "debugger/ttd/engine/ttdsessionfile.h"

namespace ttd
{

class TimeTravelEngine;

class TTDRecordingWriter
{
public:
    /// @p segmentPath: the path of segment file n (TTDRecordingFolder::SegmentPath)
    using SegmentPathFn = std::function<std::string(uint32_t)>;

    TTDRecordingWriter(SegmentPathFn segmentPath, const TTDSessionSaveParams& params, bool background = true)
        : _segmentPath(std::move(segmentPath)), _params(params), _background(background)
    {
    }
    ~TTDRecordingWriter();

    /// The first segment's file (the session has its first checkpoint)
    bool Begin(const TimeTravelEngine& engine, std::string& error);
    /// After each capture: a new segment closes the current file and opens the next
    bool Collect(const TimeTravelEngine& engine);
    /// At stop: the current file gets the rest and its index
    bool Finish(const TimeTravelEngine& engine);

    bool Failed() const { return !_error.empty() || (_writer && _writer->Failed()); }
    std::string Error() const;
    uint32_t SegmentFiles() const { return _segment + (_writer ? 1 : 0); }

private:
    bool Open(const TimeTravelEngine& engine, size_t first, std::string& error);
    bool Close(const TimeTravelEngine& engine, size_t end);

    SegmentPathFn _segmentPath;
    TTDSessionSaveParams _params;
    bool _background;
    uint32_t _segment = 0;          ///< files closed
    size_t _first = 0;              ///< the open file's first checkpoint
    std::unique_ptr<TTDFileSink> _sink;
    std::unique_ptr<TTDSessionWriter> _writer;
    std::string _error;
};

/// One .ttd from the files of a recording, in order: their records copied as
/// stored, parts renumbered, each file's start kept (the loader restarts its
/// numbering there). Refuses files of another recording (other tables) and an
/// existing @p target unless @p overwrite
bool JoinSessionFiles(const std::vector<std::string>& files, const std::string& target, bool overwrite,
                      std::string& error);

/// Load a recording's segment files into @p engine: the last @p windowFrames
/// (whole files, from the end back until they cover it), or all of them with 0
bool LoadRecording(TimeTravelEngine& engine, const std::vector<std::string>& files, uint64_t windowFrames,
                   std::string& error, TTDSessionLoadReport* report = nullptr);

}  // namespace ttd
