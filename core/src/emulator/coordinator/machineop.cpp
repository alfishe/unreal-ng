#include "machineop.h"

namespace coordinator
{
namespace
{
thread_local OpSource t_source = OpSource::Unknown;
}  // namespace

OpSource CurrentSource()
{
    return t_source;
}

void SetThreadSource(OpSource source)
{
    t_source = source;
}

SourceScope::SourceScope(OpSource source) : _previous(t_source)
{
    t_source = source;
}

SourceScope::~SourceScope()
{
    t_source = _previous;
}

const char* OpKindName(OpKind kind)
{
    switch (kind)
    {
        case OpKind::Reset: return "reset";
        case OpKind::Autostart: return "autostart";
        case OpKind::SnapshotLoad: return "snapshot-load";
        case OpKind::MediaInsert: return "media-insert";
        case OpKind::MediaEject: return "media-eject";
        case OpKind::MediaDiscard: return "media-discard";
        case OpKind::DiskCreate: return "disk-create";
        case OpKind::NetworkChange: return "network-change";
        case OpKind::IdeDriveKind: return "ide-drive-kind";
        case OpKind::RzxRestore: return "rzx-restore";
        case OpKind::ModelSwitch: return "model-switch";
        case OpKind::TimelineCut: return "timeline-cut";
        case OpKind::RomLoad: return "rom-load";
        case OpKind::GsCardSwitch: return "gs-card-switch";
        case OpKind::SpeedChange: return "speed-change";
        case OpKind::StateTransfer: return "state-transfer";
        case OpKind::SlotChange: return "slot-change";
        case OpKind::CdFrontPanel: return "cd-front-panel";
        case OpKind::ToolEdit: return "tool-edit";
        case OpKind::TtdStart: return "ttd-start";
        case OpKind::TtdStop: return "ttd-stop";
        case OpKind::TtdDiscard: return "ttd-discard";
        case OpKind::TimelineSeek: return "timeline-seek";
        case OpKind::TtdLoad: return "ttd-load";
        case OpKind::Count: break;
    }
    return "unknown";
}

const char* OpSourceName(OpSource source)
{
    switch (source)
    {
        case OpSource::Unknown: return "unknown";
        case OpSource::Qt: return "qt";
        case OpSource::WebApi: return "webapi";
        case OpSource::Cli: return "cli";
        case OpSource::Lua: return "lua";
        case OpSource::Python: return "python";
        case OpSource::Gdb: return "gdb";
        case OpSource::DeZog: return "dezog";
        case OpSource::Mcp: return "mcp";
        case OpSource::Companion: return "companion";
        case OpSource::Machine: return "machine";
        case OpSource::Core: return "core";
        case OpSource::Test: return "test";
        case OpSource::HostDevice: return "host-device";
        case OpSource::Count: break;
    }
    return "unknown";
}

const char* MachineModeName(MachineMode mode)
{
    switch (mode)
    {
        case MachineMode::Live: return "live";
        case MachineMode::Recording: return "recording";
        case MachineMode::BlackBox: return "black-box";
        case MachineMode::Replaying: return "replaying";
        case MachineMode::Detached: return "detached";
        case MachineMode::Count: break;
    }
    return "unknown";
}

const char* SessionEffectName(SessionEffect effect)
{
    switch (effect)
    {
        case SessionEffect::None: return "none";
        case SessionEffect::EndKeepHistory: return "end-keep-history";
        case SessionEffect::MediaChanged: return "media-changed";
        case SessionEffect::StopThenMediaChanged: return "stop-then-media-changed";
        case SessionEffect::MachineChanged: return "machine-changed";
        case SessionEffect::TimingChanged: return "timing-changed";
    }
    return "unknown";
}

const char* OpStatusName(OpStatus status)
{
    switch (status)
    {
        case OpStatus::Done: return "done";
        case OpStatus::Refused: return "refused";
        case OpStatus::Failed: return "failed";
    }
    return "unknown";
}
}  // namespace coordinator
