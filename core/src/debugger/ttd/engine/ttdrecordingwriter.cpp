#include "debugger/ttd/engine/ttdrecordingwriter.h"

#include <filesystem>

#include "common/filehelper.h"
#include "debugger/ttd/timetravelengine.h"

namespace ttd
{

/// region <Writer>

TTDRecordingWriter::~TTDRecordingWriter() = default;

std::string TTDRecordingWriter::Error() const
{
    if (!_error.empty())
        return _error;
    return _writer ? _writer->Error() : std::string();
}

bool TTDRecordingWriter::Open(const TimeTravelEngine& engine, size_t first, std::string& error)
{
    const std::string path = _segmentPath(_segment);
    _sink = std::make_unique<TTDFileSink>(path);
    if (!_sink->Valid())
    {
        error = path + ": " + _sink->Error();
        return false;
    }
    _writer = std::make_unique<TTDSessionWriter>(_background);
    _first = first;
    return _writer->Begin(engine, *_sink, _params, error, {}, first);
}

bool TTDRecordingWriter::Close(const TimeTravelEngine& engine, size_t end)
{
    const bool ok = _writer->Finish(engine, end);
    if (!ok && _error.empty())
        _error = _writer->Error();
    _sink->Close();
    _writer.reset();
    _sink.reset();
    ++_segment;
    return ok;
}

bool TTDRecordingWriter::Begin(const TimeTravelEngine& engine, std::string& error)
{
    if (engine.Segments().empty())
    {
        error = "the session has no checkpoint yet";
        return false;
    }
    if (!Open(engine, engine.Segments().back().firstCheckpoint, error))
    {
        _error = error;
        return false;
    }
    return true;
}

bool TTDRecordingWriter::Collect(const TimeTravelEngine& engine)
{
    if (!_writer || Failed())
        return false;
    // A segment that started after the open file's: that file ends at its baseline
    for (const TTDSegmentInfo& s : engine.Segments())
    {
        if (s.firstCheckpoint <= _first)
            continue;
        if (!Close(engine, s.firstCheckpoint))
            return false;
        std::string error;
        if (!Open(engine, s.firstCheckpoint, error))
        {
            _error = error;
            return false;
        }
    }
    return _writer->Collect(engine);
}

bool TTDRecordingWriter::Finish(const TimeTravelEngine& engine)
{
    if (!_writer)
        return !Failed();
    if (!Collect(engine) && !_writer)
        return false;
    return Close(engine, SIZE_MAX);
}

/// endregion </Writer>

/// region <Join and load>

bool JoinSessionFiles(const std::vector<std::string>& files, const std::string& target, bool overwrite,
                      std::string& error)
{
    if (files.empty())
    {
        error = "nothing to join";
        return false;
    }
    std::error_code ec;
    if (std::filesystem::exists(FileHelper::ToFsPath(target), ec))
    {
        if (!overwrite)
        {
            error = target + " exists";
            return false;
        }
        std::filesystem::remove(FileHelper::ToFsPath(target), ec);
    }
    // Written beside the target and renamed: a failed join leaves no half file under the name
    const std::string temp = target + ".partial";
    std::filesystem::remove(FileHelper::ToFsPath(temp), ec);
    bool ok = true;
    {
        TTDFileSink sink(temp);
        if (!sink.Valid())
        {
            error = temp + ": " + sink.Error();
            return false;
        }
        TTDContainerWriter writer;
        std::vector<uint8_t> tables;
        uint32_t partBase = 0;
        for (size_t f = 0; ok && f < files.size(); ++f)
        {
            TTDFileSource source(files[f]);
            TTDContainerReader reader;
            if (!source.Valid() || !reader.Open(source, error, TTDSessionFile::KnownStream))
            {
                if (error.empty())
                    error = files[f] + ": " + source.Error();
                ok = false;
                break;
            }
            if (f == 0)
            {
                tables = reader.Header().sessionTables;
                if (!writer.Begin(sink, reader.Header(), &error))
                {
                    ok = false;
                    break;
                }
            }
            else if (reader.Header().sessionTables != tables)
            {
                error = files[f] + " is not of the same recording";
                ok = false;
                break;
            }
            for (const TTDPartRef& part : reader.Parts())
            {
                if (part.damaged)
                {
                    error = files[f] + ", part " + std::to_string(part.index) + ": " + part.damage;
                    ok = false;
                    break;
                }
                std::vector<uint8_t> stored;
                for (const TTDRecordRef& record : part.records)
                    if (!reader.ReadStored(record, stored, &error) ||
                        !writer.AddStoredRecord(record.streamId, record.flags, stored.data(), stored.size(),
                                                record.rawSize))
                    {
                        if (error.empty())
                            error = "the joined file could not be written";
                        ok = false;
                        break;
                    }
                if (!ok)
                    break;
                TTDPartEnd end;
                end.firstFrame = part.firstFrame;
                end.frameCount = part.frameCount;
                end.branch = part.branch;
                end.extra = part.extra;
                for (uint32_t d : part.dependencies)
                    end.dependencies.push_back(partBase + d);
                if (!writer.EndPart(end))
                {
                    error = "the joined file could not be written";
                    ok = false;
                    break;
                }
            }
            partBase += static_cast<uint32_t>(reader.Parts().size());
        }
        ok = ok && writer.Finalize();
        if (!ok && error.empty())
            error = "the joined file could not be written";
        sink.Close();
    }
    if (ok)
        std::filesystem::rename(FileHelper::ToFsPath(temp), FileHelper::ToFsPath(target), ec);
    if (!ok || ec)
    {
        if (ok)
            error = "cannot rename to " + target + ": " + ec.message();
        std::filesystem::remove(FileHelper::ToFsPath(temp), ec);
        return false;
    }
    return true;
}

bool LoadRecording(TimeTravelEngine& engine, const std::vector<std::string>& files, uint64_t windowFrames,
                   std::string& error, TTDSessionLoadReport* report)
{
    if (files.empty())
    {
        error = "no segment file";
        return false;
    }
    // From the last file back until the files cover the window
    size_t from = 0;
    if (windowFrames > 0)
    {
        uint64_t covered = 0;
        from = files.size();
        while (from > 0 && covered < windowFrames)
        {
            --from;
            TTDFileSource source(files[from]);
            TTDContainerReader reader;
            std::string ignored;
            if (!source.Valid() || !reader.Open(source, ignored) || reader.Parts().empty())
                continue;
            const TTDPartRef& first = reader.Parts().front();
            const TTDPartRef& last = reader.Parts().back();
            covered += last.firstFrame + last.frameCount - first.firstFrame;
        }
    }
    TTDSessionLoadReport total;
    for (size_t f = from; f < files.size(); ++f)
    {
        TTDFileSource source(files[f]);
        if (!source.Valid())
        {
            error = files[f] + ": " + source.Error();
            return f > from;
        }
        TTDSessionLoadReport one;
        if (!TTDSessionFile::Load(engine, source, error, &one, f > from))
            return f > from;
        total.checkpoints = one.checkpoints;
        total.partsLoaded += one.partsLoaded;
        total.partsInFile += one.partsInFile;
        total.notes.insert(total.notes.end(), one.notes.begin(), one.notes.end());
        if (!one.complete)
        {
            total.stoppedAt = files[f] + ", " + one.stoppedAt;
            break;
        }
    }
    total.complete = total.stoppedAt.empty();
    if (report)
        *report = std::move(total);
    return true;
}

/// endregion </Join and load>

}  // namespace ttd
