#pragma once

// The time travel commands the TTD panel and the Embedded debugger share (TTD branches debuggers.md §4): the branch
// management with its questions, Branch Here / Continue Rec, the fork points of the shown timeline and the one-line
// description of where the machine is. Every command is a TTDControl verb on the instance; a refusal is shown

#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "debugger/ttd/ttdsessiontypes.h"

class Emulator;
class QWidget;

namespace ttdgui
{

/// A branch that leaves the shown timeline at a frame (ttd::TTDForkPoints)
using ForkLeave = ttd::TTDForkLeave;

/// What a debugger's state text says about the moment (debuggers.md §4.2), in parts: the debugger shows the branch
/// part on its branch drop-down
struct MomentText
{
    QString state;    ///< "Recording" | "History: frame 1234 of 1900, replaying" | "Live, not recorded"; empty: no history
    QString branch;   ///< "on '+00:00:02.785' (3 branches)"; empty with the trunk alone
    QString fork;     ///< "fork here: 2 other futures"; empty where no other branch leaves

    /// The parts joined by " · "
    QString Line() const;
};

/// frame: the machine's frame now (it may have run past the end of a stopped recording)
MomentText DescribeMoment(const ttd::TTDSessionInfo& info, uint64_t frame);

/// The branches leaving at the frame (from ForkPoints), none without branches
std::vector<ForkLeave> ForksAt(const ttd::TTDSessionInfo& info, uint64_t frame);

class BranchCommands
{
public:
    /// parent: the questions' and refusals' window; emulator: the instance the commands act on (asked each time)
    BranchCommands(QWidget* parent, std::function<std::shared_ptr<Emulator>()> emulator);

    /// Asks before a branch is deleted (title, question; true = go ahead). A message box by default; the tests
    /// answer without one
    std::function<bool(const QString&, const QString&)> confirm;
    /// The machine moved in time (a switch, a seek): the views redraw
    std::function<void()> moved;
    /// After every command, done or refused: the owner refreshes
    std::function<void()> changed;

    /// A verb on the instance; a refusal is shown under title. Returns the reply's success
    bool Run(const std::string& verb, std::map<std::string, std::string> options, const QString& title);

    bool Switch(uint32_t branch);
    void Rename(uint32_t branch);
    void Tags(uint32_t branch);
    void Note(uint32_t branch);
    void TogglePin(uint32_t branch);
    /// Asks first (the branches leaving it, pins, a switch to its parent when the timeline runs on it); never the
    /// trunk. True when it is gone
    bool Delete(uint32_t branch);
    void SaveLineage();

    /// Branch Here / Continue Rec (owner Q-1 (a)): recording goes on from where the machine stands in the history (a
    /// new branch in the past), or from the end of a stopped recording (the machine goes back there first). Media
    /// that cannot go back get the user's decision. runAfter: the machine runs on (the TTD panel); the debugger
    /// leaves it paused for the next step. True when recording goes on
    bool ResumeRecordingHere(const QString& title, bool runAfter);

private:
    /// The published branch, kept alive by keep; null when gone
    const ttd::TTDBranchInfo* Published(uint32_t branch, std::shared_ptr<const ttd::TTDBranchesView>& keep) const;
    void Changed() const;

    QWidget* _parent = nullptr;
    std::function<std::shared_ptr<Emulator>()> _emulator;
};

}  // namespace ttdgui
