// ZX-bus slots reference data: documented real-world pair rules that the functions and claims alone do not express
// (rule D9, evaluated first). RefData_Test.CollectionIsConsistent refuses an entry the claims already explain.

#include "refdata.h"

#include "emulator/platform.h"

namespace slots::refdata
{

namespace
{

constexpr Src kMoonsoundProfiSources[] = { Src::RepoProfi1024, Src::AlfisheZxmMoonsound, Src::RepoSlotsResearchCards };
constexpr Src kGsGmxSources[] = { Src::GmxApollonov };

constexpr ExceptionDef kExceptions[] = {
    // The claims say only "both latch #7E writes": the palette is not shown masked by /OUTIORQ and the card's IORQGE
    // means nothing on a board-wins bus, so every OPL4 wave-port write also reprograms the palette. The repo refuses
    // the pair (docs/hardware/profi-1024.md); whether OUTIORQ helps is unconfirmed.
    { .card = "moonsound", .model = MM_PROFI, .outcome = Outcome::Refused, .hard = true,
      .reason = "`#7E` is the Profi palette port; the board wins, every wave-port write would reprogram the palette",
      .brief = "`#7E` palette, board wins", .sources = kMoonsoundProfiSources },
    // The ports do not clash; the 1999 article says the classic General Sound does not work with the card. The
    // official site later claimed a firmware of 20.04.99 fixed it, so a forced plug is allowed (GMX design Q14)
    { .card = "gs", .model = MM_GMX, .outcome = Outcome::Refused, .hard = false,
      .reason = "General Sound does not work with the GMX card (Apollonov 1999); a later firmware claims a fix",
      .brief = "does not work with GMX (1999)", .sources = kGsGmxSources },
    { .card = "gs-lw", .model = MM_GMX, .outcome = Outcome::Refused, .hard = false,
      .reason = "General Sound does not work with the GMX card (Apollonov 1999); a later firmware claims a fix",
      .brief = "does not work with GMX (1999)", .sources = kGsGmxSources },
};

} // namespace

std::span<const ExceptionDef> Exceptions()
{
    return kExceptions;
}

} // namespace slots::refdata
