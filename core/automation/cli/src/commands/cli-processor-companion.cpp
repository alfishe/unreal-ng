// Companion clients on the CLI (docs/inprogress/2026-10-16-companion-client/surfaces.md §3): a connection that
// authenticates with a companion token acts as the user's hands - its user actions open no session - and every
// connection can list and revoke companions. Approval of a pairing is the GUI's alone

#include "cli-processor.h"

#include <sstream>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/companionactions.h"
#include "emulator/execution/companionregistry.h"

namespace
{
std::string ScopesText(uint32_t scopes)
{
    std::string text;
    for (const std::string& name : automation_gate::CompanionScopeNames(scopes))
        text += (text.empty() ? "" : ", ") + name;
    return text.empty() ? std::string("no scopes") : text;
}

std::string HolderText(const EmulatorContext::RunControlState& state)
{
    return state.surfaceLabel + (state.client.empty() ? std::string() : " (" + state.client + ")");
}
}  // namespace

bool CLIProcessor::PassCompanionGate(const ClientSession& session, const std::string& command,
                                     const std::vector<std::string>& args)
{
    if (!automation_gate::CliCommandChangesMachine(command, args))
        return true;
    const std::optional<companion::CompanionInfo> me = companion::CompanionRegistry::Instance().Find(_companionId);
    auto emulator = GetSelectedEmulator(session);
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!me || !context)
        return true;  // the handler answers (no emulator), or ProcessCommand already refused a revoked companion
    const EmulatorContext::RunControlState state = context->GetRunControlState();
    const companion::ActionCheck check =
        companion::CheckAction(*me, automation_gate::CompanionCliScopeOf(command, args), state.claimed,
                               state.claimed ? HolderText(state) : std::string(), "'companion take-back'",
                               automation_gate::CompanionCliNeedsPause(command, args),
                               emulator->GetState() == StatePaused);
    if (check.allowed)
        return true;
    session.SendResponse("Error: " + check.error + " - " + check.message + NEWLINE);
    return false;
}

void CLIProcessor::HandleCompanion(const ClientSession& session, const std::vector<std::string>& args)
{
    companion::CompanionRegistry& registry = companion::CompanionRegistry::Instance();
    const std::string sub = args.empty() ? std::string("status") : args[0];
    const std::optional<companion::CompanionInfo> me =
        _companionId.empty() ? std::nullopt : registry.Find(_companionId);

    if (sub == "help" || sub == "-h" || sub == "--help")
    {
        std::ostringstream oss;
        oss << "Companion clients: a paired device acting as the user's hands (its changes open no session)" << NEWLINE
            << "  companion                         this connection and the instance's control state" << NEWLINE
            << "  companion auth <token>            act as that companion on this connection" << NEWLINE
            << "  companion logout                  back to an ordinary connection" << NEWLINE
            << "  companion list                    companions and pending pairings" << NEWLINE
            << "  companion revoke <id|name|all|me> revoke a companion" << NEWLINE
            << "  companion take-back               take control back from a session (needs takeback)" << NEWLINE
            << "  companion allow-again             let clients in a cooldown take control again (needs takeback)"
            << NEWLINE << "Pairing is approved at the GUI only." << NEWLINE;
        session.SendResponse(oss.str());
        return;
    }

    if (sub == "auth")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Usage: companion auth <token>") + NEWLINE);
            return;
        }
        auto emulator = GetSelectedEmulator(session);
        EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
        if (context && HoldsControl(context))
        {
            session.SendResponse(std::string("Error: companion_session - this connection holds a session; 'session "
                                             "release' first") + NEWLINE);
            return;
        }
        const std::optional<companion::CompanionInfo> found = registry.Authenticate(args[1]);
        if (!found)
        {
            session.SendResponse(std::string("Error: companion_unknown - unknown or revoked companion token") + NEWLINE);
            return;
        }
        _companionId = found->id;
        session.SendResponse("Companion '" + found->name + "' (" + ScopesText(found->scopes) +
                             "). Changes on this connection open no session." + NEWLINE);
        return;
    }

    if (sub == "logout")
    {
        _companionId.clear();
        session.SendResponse(std::string("This connection is an ordinary one again") + NEWLINE);
        return;
    }

    if (sub == "list")
    {
        std::ostringstream oss;
        const std::vector<companion::CompanionInfo> companions = registry.List();
        oss << companions.size() << " companion(s)" << NEWLINE;
        for (const companion::CompanionInfo& c : companions)
            oss << "  " << c.id << "  " << c.name << (c.device.empty() ? "" : " (" + c.device + ")") << "  ["
                << ScopesText(c.scopes) << "]  " << c.source << (c.id == _companionId ? "  - this connection" : "")
                << NEWLINE;
        const std::vector<companion::PairingInfo> pending = registry.Pending();
        if (!pending.empty())
        {
            oss << pending.size() << " pairing(s) waiting for the user at the GUI" << NEWLINE;
            for (const companion::PairingInfo& p : pending)
                oss << "  " << p.id << "  " << p.name << " from " << p.peer << ", " << p.expiresIn << " s left"
                    << NEWLINE;
        }
        if (!registry.IsApprovalAvailable())
            oss << "No GUI on this instance: companions come from UNREAL_COMPANION_TOKENS" << NEWLINE;
        else if (!registry.IsPairingAllowed())
            oss << "The user turned pairing requests off at the GUI" << NEWLINE;
        session.SendResponse(oss.str());
        return;
    }

    if (sub == "revoke")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Usage: companion revoke <id|name|all|me>") + NEWLINE);
            return;
        }
        std::string target = args[1];
        if (target == "me")
        {
            if (!me)
            {
                session.SendResponse(std::string("Error: this connection is not a companion") + NEWLINE);
                return;
            }
            target = me->id;
        }
        const size_t revoked = registry.Revoke(target);
        if (!revoked)
        {
            session.SendResponse("Error: unknown_companion - no companion '" + target + "'" + NEWLINE);
            return;
        }
        session.SendResponse(std::to_string(revoked) + " companion(s) revoked" + NEWLINE);
        return;
    }

    auto emulator = GetSelectedEmulator(session);
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;

    if (sub == "take-back" || sub == "allow-again")
    {
        if (!me)
        {
            session.SendResponse(std::string("Error: companion_required - only a companion does this ('companion "
                                             "auth <token>' first)") + NEWLINE);
            return;
        }
        if (!(me->scopes & automation_gate::ScopeTakeBack))
        {
            session.SendResponse(std::string("Error: companion_forbidden - this companion may not use 'takeback'") +
                                 NEWLINE);
            return;
        }
        if (!context)
        {
            session.SendResponse(std::string("Error: no emulator selected") + NEWLINE);
            return;
        }
        if (sub == "allow-again")
        {
            context->AllowRunControlRetake();
            session.SendResponse(std::string("Clients in a cooldown may take control again") + NEWLINE);
            return;
        }
        const EmulatorContext::RunControlState state = context->GetRunControlState();
        if (!state.claimed || !context->RevokeRunControl("companion '" + me->name + "'"))
        {
            session.SendResponse(std::string("Error: nothing_held - no session holds this emulator") + NEWLINE);
            return;
        }
        session.SendResponse("Control taken back from " + HolderText(state) + NEWLINE);
        return;
    }

    if (sub != "status")
    {
        session.SendResponse("Unknown companion command: " + sub + " ('companion help')" + NEWLINE);
        return;
    }
    std::string text = me ? "Companion '" + me->name + "' (" + ScopesText(me->scopes) + ")"
                          : std::string("This connection is not a companion ('companion auth <token>')");
    if (context)
    {
        const EmulatorContext::RunControlState state = context->GetRunControlState();
        text += std::string(NEWLINE) +
                (state.claimed ? "Run control held by " + HolderText(state) +
                                     (me && (me->scopes & automation_gate::ScopeTakeBack)
                                          ? " - this companion may take it back"
                                          : std::string())
                               : std::string("Run control: free"));
        // What this companion may do now, and why the rest not (the same catalogue as GET /api/v1/companions/me)
        if (me)
        {
            const StateNode actions = companion::ActionsReport(*me, companion::ViewOf(*emulator));
            std::string now, notNow;
            for (const auto& [name, node] : actions.members)
            {
                const StateNode* available = node.find("available");
                if (available && available->b)
                    now += (now.empty() ? "" : ", ") + name;
                else if (const StateNode* reason = node.find("reason"))
                    notNow += std::string(NEWLINE) + "  " + name + ": " + reason->s;
            }
            text += std::string(NEWLINE) + "Available now: " + (now.empty() ? std::string("nothing") : now);
            if (!notNow.empty())
                text += std::string(NEWLINE) + "Not now:" + notNow;
        }
    }
    session.SendResponse(text + NEWLINE);
}
