/// @author rfishe
/// @date 08.01.2026
/// @brief CLI Snapshot control commands handler

#include "cli-processor.h"

#include <debugger/ttd/timetravelmanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <common/filehelper.h>
#include <loaders/snapshot/snapshotlauncher.h>

#include <sstream>

/// region <Snapshot Control Commands>

void CLIProcessor::HandleSnapshot(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        return;
    }

    auto context = emulator->GetContext();
    if (!context)
    {
        session.SendResponse(std::string("Error: Emulator context not available") + NEWLINE);
        return;
    }

    if (args.empty())
    {
        ShowSnapshotHelp(session);
        return;
    }

    std::string subcommand = args[0];

    if (subcommand == "load")
    {
        HandleSnapshotLoad(session, emulator, context, args);
    }
    else if (subcommand == "save")
    {
        HandleSnapshotSave(session, emulator, args);
    }
    else if (subcommand == "info")
    {
        HandleSnapshotInfo(session, emulator, context);
    }
    else if (subcommand == "inspect")
    {
        HandleSnapshotInspect(session, emulator, args);
    }
    else if (subcommand == "formats")
    {
        HandleSnapshotFormats(session, emulator);
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown subcommand '") + args[0] + "'" + NEWLINE +
                             "Use 'snapshot' without arguments to see available subcommands." + NEWLINE);
    }
}

void CLIProcessor::HandleSnapshotLoad(const ClientSession& session,
                                      std::shared_ptr<Emulator> emulator,
                                      EmulatorContext* context,
                                      const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing file path") + NEWLINE +
                            "Usage: snapshot load <file> [--no-switch] [--commit <name>]" + NEWLINE);
        return;
    }

    std::string filepath = args[1];
    bool switchModel = true;
    std::string commit;
    for (size_t i = 2; i < args.size(); i++)
    {
        if (args[i] == "--no-switch")
            switchModel = false;
        else if (args[i] == "--commit" && i + 1 < args.size())
            commit = args[++i];
        else
        {
            session.SendResponse("Error: Unknown option '" + args[i] + "'" + NEWLINE);
            return;
        }
    }

    // TTD refuses a snapshot load while recording (it would drop the history)
    if (const std::string refusal = emulator->RecordingGuard(ttd::TTDGuardedAction::LoadSnapshot); !refusal.empty())
    {
        session.SendResponse("Error: " + refusal + NEWLINE);
        return;
    }

    // A file that needs another model (an SPG: TS-Conf) switches it first,
    // as on every surface (snapshotlauncher.h)
    SnapshotLoadRequest request;
    request.emulatorId = emulator->GetId();
    request.path = filepath;
    request.switchModel = switchModel;
    request.commit = commit;
    emulator.reset();
    const SnapshotLoadResult result = SnapshotLauncher::Load(request);

    std::stringstream ss;
    if (!result.ok)
    {
        ss << "Error: " << result.message << NEWLINE;
        if (result.modelMismatch)
            ss << "Switch with 'model " << result.requiredModel << "', or load without --no-switch" << NEWLINE;
        if (result.report.refused && !result.report.needs.empty())
            ss << "Needs: " << result.report.needs << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }
    if (result.modelSwitched)
        ss << "Switched to " << result.requiredModel << " for the file" << NEWLINE
           << "New emulator instance: " << result.emulator->GetId() << NEWLINE;
    ss << "Snapshot loaded: " << filepath << NEWLINE;
    if (!result.report.format.empty())
        ss << "Commit: " << result.report.commit << NEWLINE;
    for (const std::string& warning : result.report.warnings)
        ss << "Warning: " << warning << NEWLINE;
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleSnapshotInspect(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                         const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing file path") + NEWLINE +
                             "Usage: snapshot inspect <file> [--commit <name>]" + NEWLINE);
        return;
    }
    std::string commit;
    for (size_t i = 2; i < args.size(); i++)
    {
        if (args[i] == "--commit" && i + 1 < args.size())
            commit = args[++i];
        else
        {
            session.SendResponse("Error: Unknown option '" + args[i] + "'" + NEWLINE);
            return;
        }
    }

    StateNode result;
    std::string error;
    if (!SnapshotLauncher::Inspect(emulator->GetId(), args[1], commit, result, error))
    {
        session.SendResponse("Error: " + error + NEWLINE);
        return;
    }

    auto text = [](const StateNode* n) {
        if (!n)
            return std::string();
        switch (n->kind)
        {
            case StateNode::Kind::String: return n->s;
            case StateNode::Kind::Bool: return std::string(n->b ? "yes" : "no");
            case StateNode::Kind::Int: return std::to_string(n->i);
            default: return std::string();
        }
    };
    const StateNode* image = result.find("image");
    const StateNode* plan = result.find("plan");
    std::stringstream ss;
    if (image)
    {
        size_t ramKb = 0;
        if (const StateNode* banks = image->find("banks"))
            for (const StateNode& bank : banks->items)
                if (const StateNode* size = bank.find("size"))
                    ramKb += static_cast<size_t>(size->i) / 1024;
        ss << "File:        " << text(result.find("path")) << NEWLINE;
        ss << "Format:      " << text(image->find("format")) << " " << text(image->find("format_version"))
           << ", made on " << text(image->find("machine_hint")) << NEWLINE;
        ss << "Memory:      " << text(image->find("memory_model")) << ", " << ramKb << " KB in "
           << (image->find("banks") ? image->find("banks")->items.size() : 0) << " banks" << NEWLINE;
        if (const StateNode* cpu = image->find("cpu"))
            ss << "CPU:         PC=" << text(cpu->find("pc")) << " SP=" << text(cpu->find("sp"))
               << " IM " << text(cpu->find("im")) << NEWLINE;
        if (const StateNode* paging = image->find("paging"))
        {
            ss << "Paging:     ";
            for (const auto& member : paging->members)
                ss << " " << member.first << "=" << text(&member.second);
            if (paging->members.empty())
                ss << " none in the file";
            ss << NEWLINE;
        }
        if (const StateNode* extensions = image->find("extensions"))
            for (const StateNode& e : extensions->items)
                ss << "Carries:     " << text(e.find("origin")) << " (" << text(e.find("kind")) << ")" << NEWLINE;
    }
    ss << NEWLINE << "Would load:  " << text(result.find("would_load")) << NEWLINE;
    if (plan)
    {
        ss << "Commit:      " << text(plan->find("commit")) << NEWLINE;
        if (const StateNode* verdicts = plan->find("verdicts"))
            for (const StateNode& v : verdicts->items)
                ss << "  verdict:   " << text(&v) << NEWLINE;
        if (plan->find("refused") && plan->find("refused")->b)
        {
            ss << "Refused:     " << text(plan->find("reason")) << NEWLINE;
            if (plan->find("needs"))
                ss << "Needs:       " << text(plan->find("needs")) << NEWLINE;
        }
        if (const StateNode* warnings = plan->find("warnings"))
            for (const StateNode& w : warnings->items)
                ss << "Warning:     " << text(&w) << NEWLINE;
    }
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleSnapshotInfo(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                      EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Snapshot Status" << NEWLINE;
    ss << "===============" << NEWLINE;
    ss << NEWLINE;

    if (context->coreState.snapshotFilePath.empty())
    {
        ss << "No snapshot loaded" << NEWLINE;
    }
    else
    {
        ss << "File: " << context->coreState.snapshotFilePath << NEWLINE;
    }
    if (!emulator->LastSnapshotReport().format.empty())
        ss << NEWLINE << emulator->LastSnapshotReport().ToText();

    session.SendResponse(ss.str());
}

void CLIProcessor::ShowSnapshotHelp(const ClientSession& session)
{
    std::stringstream ss;
    ss << "Snapshot Commands" << NEWLINE;
    ss << "=================" << NEWLINE;
    ss << NEWLINE;
    ss << "  snapshot load <file> [--no-switch] [--commit <name>]" << NEWLINE;
    ss << "                                 Load snapshot from file (.z80, .sna, .szx; .rzx plays on this machine;" << NEWLINE;
    ss << "                                 .spg - a TS-Conf program - switches to model TSL unless --no-switch)" << NEWLINE;
    ss << "  snapshot save <file> [--force] Save snapshot to file (.sna, .z80, .szx: by extension)" << NEWLINE;
    ss << "                                 --commit: who writes the machine - omitted: the machine's own policy," << NEWLINE;
    ss << "                                 else the legacy commit; 'legacy': the legacy commit always; or a policy name" << NEWLINE;
    ss << "  snapshot inspect <file> [--commit <name>]" << NEWLINE;
    ss << "                                 What loading the file would do here (its contents, who would commit, or" << NEWLINE;
    ss << "                                 why it is refused); nothing is written" << NEWLINE;
    ss << "  snapshot formats               Which formats this machine can be saved in right now, and why not" << NEWLINE;
    ss << "  snapshot info                  Get current snapshot status and the last load's pipeline report" << NEWLINE;
    ss << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleSnapshotSave(const ClientSession& session,
                                      std::shared_ptr<Emulator> emulator,
                                      const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing file path") + NEWLINE +
                            "Usage: snapshot save <file> [--force]" + NEWLINE);
        return;
    }

    std::string filepath = args[1];
    
    // Check for --force flag
    bool force = false;
    for (size_t i = 2; i < args.size(); i++)
    {
        if (args[i] == "--force" || args[i] == "-f")
        {
            force = true;
        }
    }
    
    // Check if file exists and force wasn't specified
    if (!force && FileHelper::FileExists(filepath))
    {
        session.SendResponse(std::string("Error: File already exists: ") + filepath + NEWLINE +
                            "Use --force to overwrite." + NEWLINE);
        return;
    }

    // Use SaveSnapshot method; a refusal carries the reason and what would work
    const bool success = emulator->SaveSnapshot(filepath);
    const snapshot::SaveResult& saved = emulator->LastSaveResult();

    if (success)
    {
        std::string text = std::string("Snapshot saved: ") + filepath + " (" + saved.format + ", " + saved.machine + ")" + NEWLINE;
        for (const std::string& warning : saved.warnings)
            text += "  note: " + warning + NEWLINE;
        session.SendResponse(text);
    }
    else
    {
        std::string text = std::string("Error: ") + saved.text + NEWLINE;
        if (!saved.needs.empty())
            text += "  needs: " + saved.needs + NEWLINE;
        text += std::string("Use 'snapshot formats' to see what this machine can be saved as.") + NEWLINE;
        session.SendResponse(text);
    }
}

void CLIProcessor::HandleSnapshotFormats(const ClientSession& session, std::shared_ptr<Emulator> emulator)
{
    const snapshot::SaveFormats formats = emulator->SnapshotSaveFormats();
    std::stringstream ss;
    ss << "Snapshot formats for this machine" << NEWLINE;
    ss << "  machine: " << (formats.machine.empty() ? "-" : formats.machine) << NEWLINE;
    ss << "  view:    " << formats.view << NEWLINE;
    for (const snapshot::FormatStatus& status : formats.formats)
    {
        ss << "  ." << snapshot::ToText(status.format) << ": " << (status.available ? "yes" : "no");
        if (!status.available)
            ss << " - " << status.reason << (status.needs.empty() ? "" : " [needs: " + status.needs + "]");
        else if (!status.note.empty())
            ss << " (" << status.note << ")";
        ss << NEWLINE;
    }
    session.SendResponse(ss.str());
}

/// endregion </Snapshot Control Commands>
