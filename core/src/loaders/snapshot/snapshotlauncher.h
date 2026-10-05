#pragma once

/// @file snapshotlauncher.h
/// @brief The one entry point the automation surfaces (WebAPI / MCP, CLI, Lua,
/// Python) use to load a snapshot, so a file that needs a particular machine
/// behaves the same everywhere: loaded on the given emulator, or, when it
/// needs another model, the model is switched first (ModelSwitch, media kept)
/// and the file loads on the new machine. The Qt window does the same through
/// its own model switch (it rebinds its views).
///
/// Which files need a model: an SPG program runs on TS-Conf only
/// (LoaderSPG::kModel). Other snapshots load on the running machine as before.
///
/// Worked example: Load({emulatorId: "e1" (a Pentagon), path: "wc.spg"})
/// -> RequiredModel says "TSL" 4096 KB -> ModelSwitch builds a TS-Conf "e2",
/// e1 is released -> the SPG loads on e2; the result carries e2 and
/// modelSwitched = true, previousEmulatorId = "e1".

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "emulator/state/statenode.h"
#include "loaders/snapshot/snapshotreport.h"

class Emulator;

struct SnapshotLoadRequest
{
    std::string emulatorId;
    std::string path;
    bool switchModel = true;  ///< the file needs another model: switch (true) or refuse (false)
    /// Who commits the snapshot into the machine: "" = the plan decides (the machine's policy, else today's commit),
    /// "legacy" = today's commit, or a registered policy name (snapshot pipeline, PLAN #84)
    std::string commit;
    /// The old machine is about to be released (a GUI unbinds its views)
    std::function<void(Emulator& old)> beforeRelease;
};

struct SnapshotLoadResult
{
    bool ok = false;
    std::string message;                 ///< why not, when !ok
    std::shared_ptr<Emulator> emulator;  ///< the machine that holds the snapshot (the new one after a switch)
    bool modelSwitched = false;
    std::string previousEmulatorId;      ///< set when the model was switched
    std::string requiredModel;           ///< the model the file needs ("TSL"), empty = any
    uint32_t requiredRamKb = 0;
    bool modelMismatch = false;          ///< refused: needs requiredModel and switching was off
    /// What the snapshot pipeline did: the commit that ran, or the refusal and why, the verdicts, the format's blocks
    snapshot::Report report;
};

class SnapshotLauncher
{
public:
    static SnapshotLoadResult Load(const SnapshotLoadRequest& request);

    /// What loading `path` on the emulator would do, touching nothing: the file's image (banks as hashes, registers,
    /// paging, extensions) and the plan (who would commit, or the refusal and why). `commit` as in the request
    static bool Inspect(const std::string& emulatorId, const std::string& path, const std::string& commit,
                        StateNode& result, std::string& error);

    /// The model a snapshot file needs (false + error: the file is unreadable
    /// or not of its type); an empty model = it loads on any machine
    static bool RequiredModel(const std::string& path, std::string& model, uint32_t& ramKb, std::string& error);
};
