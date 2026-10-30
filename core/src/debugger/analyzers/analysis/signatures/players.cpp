#include "debugger/analyzers/analysis/signatures/signatures.h"

namespace analysis::signatures
{

/// Music players. Patterns from the players' own code in the repository:
///  - Vortex Tracker II PT3 player r.7: the header of docs/disasm/software/wildplayer/wildplayer_bank1_vtii.asm
///    (WildPlayer's VTII engine master): LD HL,module / JR init / JP play / JR mute / JR NZ / RET, then the
///    "=VTII PT3 Player r.7=" string; init at +0 (HL = the module, the LD's operand), play at +5. Only the 13 bytes of
///    the header are claimed as code (the string after it is data); the music detector names the play entry and
///    claims the module the player reads.
/// No beeper player has its code in the repository: none is in the catalog (the music detector's AY behaviour rule
/// does not cover beepers either).
const std::vector<Signature>& Players()
{
    static const std::vector<Signature> catalog = {
        {"players.vtii-pt3-r7", "players", "VTII_INIT",
         "21 ?? ?? 18 ?? C3 ?? ?? 18 ?? 20 ?? C9 3D 56 54 49 49 20 50 54 33 20 50 6C 61 79 65 72 20 72 2E 37 3D", 0,
         13, RegionKind::Code, SignatureCheck::Entry, "Vortex Tracker II PT3 player r.7: init (HL = the module)"},
    };
    return catalog;
}

}  // namespace analysis::signatures
