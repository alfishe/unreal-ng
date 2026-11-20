#pragma once

/// @file debugboards.h
/// @brief Device boards (debugger-model widget catalog §4, protocol §3.10 / §6.4): a device's hardware state as data,
/// a layout of columns -> groups -> controls with their values, which any front-end renders with one generic renderer.
///
/// Boards are built on read from the device's report getters (side-effect free, the same data as `DeviceState`); a
/// board's actions are requests through the device's managers (an SD card insert goes through the media manager's
/// slot `sd.ngs`). Every plane calls these: WebAPI `/debug/boards`, CLI `board`, MCP, Lua and Python.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;

/// Builds one board (the schema of widget catalog §4): `{id, title, device, cpu, available, columns[{groups[{title,
/// raw, controls[{id, label, type, value, names / values / min / max, note}]}]}], actions[{id, title, param}]}`
class DebugBoardBuilder
{
public:
    DebugBoardBuilder(std::string id, std::string title, std::string device, std::string cpu, bool available = true);

    DebugBoardBuilder& Column();
    /// A group in the current column; @p raw is the register shown next to the title ("GSCFG0=#05")
    DebugBoardBuilder& Group(const std::string& title, const std::string& raw = "");

    DebugBoardBuilder& Hex8(const std::string& id, const std::string& label, uint8_t value, const std::string& note = "");
    DebugBoardBuilder& Hex16(const std::string& id, const std::string& label, uint16_t value, const std::string& note = "");
    /// A 21-bit card address (NeoGS DMA)
    DebugBoardBuilder& Hex21(const std::string& id, const std::string& label, uint32_t value, const std::string& note = "");
    DebugBoardBuilder& Dec(const std::string& id, const std::string& label, int64_t value, const std::string& note = "");
    DebugBoardBuilder& Counter(const std::string& id, const std::string& label, uint64_t value, const std::string& note = "");
    DebugBoardBuilder& Rate(const std::string& id, const std::string& label, double value, const std::string& note = "");
    DebugBoardBuilder& Bool(const std::string& id, const std::string& label, bool value, const std::string& note = "");
    DebugBoardBuilder& Led(const std::string& id, const std::string& label, bool value, const std::string& note = "");
    DebugBoardBuilder& Text(const std::string& id, const std::string& label, const std::string& value, const std::string& note = "");
    /// A register with its bits named, bit 0 first ("" = unused bit)
    DebugBoardBuilder& Bits(const std::string& id, const std::string& label, uint8_t value, const std::vector<std::string>& names,
                            const std::string& note = "");
    DebugBoardBuilder& Enum(const std::string& id, const std::string& label, const std::string& value,
                            const std::vector<std::string>& values, const std::string& note = "");
    DebugBoardBuilder& Bar(const std::string& id, const std::string& label, int64_t value, int64_t min, int64_t max,
                           const std::string& note = "");

    /// An action the board offers; @p param names its argument ("" = none)
    DebugBoardBuilder& Action(const std::string& id, const std::string& title, const std::string& param = "");

    const std::string& Id() const { return _id; }
    /// The layout with the values; @p valuesOnly: `{id, available, values{control: value}}`
    StateNode Build(bool valuesOnly) const;
    /// The list entry: `{id, title, device, cpu, available, actions[]}`
    StateNode Summary() const;

private:
    StateNode& Add(const std::string& id, const std::string& label, const char* type, StateNode value, const std::string& note);

    std::string _id, _title, _device, _cpu;
    bool _available;
    StateNode _columns = StateNode::Array();
    StateNode _actions = StateNode::Array();
    std::vector<std::pair<std::string, StateNode>> _values;
};

/// What the planes get from a board read or action (CLI, Lua, Python, WebAPI; MCP through the WebAPI)
struct DebugBoardReply
{
    int status = 200;   ///< as HTTP: 404 no such board / action, 400 a missing argument, 409 refused (TTD), 422 failed
    StateNode body;     ///< the list, the board, the action's result, or {error}
    std::string text;   ///< the same for people
};

namespace DebugBoards
{
/// Every board the machine has now: `{boards: [summary...]}`
DebugBoardReply List(EmulatorContext* context);
/// One board, its layout and values (or the values only)
DebugBoardReply Read(EmulatorContext* context, const std::string& id, bool valuesOnly = false);
/// A board's action: `neogs_sd` insert <path> / eject, `neogs_flash` save
DebugBoardReply Action(EmulatorContext* context, const std::string& id, const std::string& action, const std::string& arg);

/// The boards as built now (tests; the planes use List / Read). @p statsBoard "": the statistics board `gs_stats`
/// without its values (reading them turns the stats publishing on), "gs_stats" with them
std::vector<DebugBoardBuilder> Build(EmulatorContext* context, const std::string& statsBoard = "gs_stats");
/// A board (Build's layout) as lines: "group: label value ..."
std::string Text(const StateNode& board);
} // namespace DebugBoards
