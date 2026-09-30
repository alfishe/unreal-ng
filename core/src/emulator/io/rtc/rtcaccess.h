#pragma once

/// @file rtcaccess.h
/// @brief Read and write the machine's CMOS clock chip from the automation
/// interfaces (CLI, WebAPI, MCP, Lua, Python). Every interface calls these
/// two functions, so they all agree on what a cell is and what a write does.
///
/// Cells are addressed as the guest addresses them (0x00 seconds ... 0x0D
/// register D, 0x0E and up RAM). Reads are side-effect free (Ds12887::
/// PeekRegister): register C keeps its flags. Writes go through the chip
/// exactly like a guest write (Ds12887::WriteRegister), so writing the time
/// registers sets the clock and writing C or D does nothing; the chip's
/// address latch is not touched. While a TTD session records, a write parks
/// the CPU and is marked as a debugger edit (Emulator::EditMemoryFromTool),
/// like a memory write from a tool.

#include <cstdint>
#include <string>
#include <vector>

class EmulatorContext;
class Ds12887;

namespace RtcAccess
{
/// The machine's clock, or null with the reason in @p reason
Ds12887* Find(EmulatorContext* context, std::string* reason = nullptr);

/// Read @p count cells from @p start into @p out. False with @p error when
/// there is no clock or the range leaves the chip
bool Read(EmulatorContext* context, unsigned start, unsigned count, std::vector<uint8_t>& out, std::string& error);

/// Write @p bytes from @p start. @p source names the tool for the TTD marker
bool Write(EmulatorContext* context, unsigned start, const std::vector<uint8_t>& bytes, const char* source,
           std::string& error);
}  // namespace RtcAccess
