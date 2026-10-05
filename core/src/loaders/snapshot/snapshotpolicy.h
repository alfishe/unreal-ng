#pragma once

/// @file snapshotpolicy.h
/// @brief Who commits a snapshot into a machine (snapshot pipeline P2, PLAN #84): the policy interface a machine or a
/// named caller choice implements, and the registry that gives a name the same meaning on every surface.
///
/// A policy is asked in the plan step (Pipeline::Plan): `Examine` says Take (I commit it), Decline (not mine, go on)
/// or Refuse (nothing is written; the reason goes back to the caller). `Commit` runs only after Take. There is no
/// "transform" verdict yet: a rewritten image only helps once the legacy commits read the image (P9), so it arrives
/// with the first policy that needs it (P5).
/// Design: docs/inprogress/2026-10-02-snapshot-pipeline/proposal.md section 4.4.

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "loaders/snapshot/snapshotimage.h"
#include "loaders/snapshot/snapshotreport.h"

class EmulatorContext;

namespace snapshot
{
struct Verdict
{
    enum class Kind : uint8_t
    {
        Decline,   ///< not my business: the next in line decides
        Take,      ///< I commit it (Commit follows)
        Refuse,    ///< nothing is written; `reason` goes back to the caller
    };

    Kind kind = Kind::Decline;
    std::string reason;   ///< shown to the user on Refuse, logged otherwise
    std::string needs;    ///< machine-readable, with a refusal: "zx_mode", "model:PENTAGON-512", ...

    static Verdict Decline() { return {}; }
    static Verdict Take(std::string why = {}) { return {Kind::Take, std::move(why), {}}; }
    static Verdict Refuse(std::string why, std::string needsHint = {})
    {
        return {Kind::Refuse, std::move(why), std::move(needsHint)};
    }
};

class ISnapshotCommitPolicy
{
public:
    virtual ~ISnapshotCommitPolicy() = default;

    /// The name callers use: "sprinter-zx", ...
    virtual std::string Name() const = 0;
    /// Look at the image and the machine, write nothing
    virtual Verdict Examine(const Image& image, EmulatorContext& context) const = 0;
    /// Write the machine from the image (after Take). False = failed; `report` says why
    virtual bool Commit(const Image& image, EmulatorContext& context, Report& report) = 0;
};

/// The named policies a caller can ask for with `commit=<name>`. "legacy" is not in here: it is the plan's own default
class SnapshotPolicies
{
public:
    /// Add (or replace) a policy under its Name()
    static void Register(std::shared_ptr<ISnapshotCommitPolicy> policy);
    /// Remove one (tests); no-op for an unknown name
    static void Unregister(const std::string& name);
    /// nullptr when no such name
    static ISnapshotCommitPolicy* Find(const std::string& name);
    /// "legacy" and every registered name, sorted
    static std::vector<std::string> Names();
};
}  // namespace snapshot
