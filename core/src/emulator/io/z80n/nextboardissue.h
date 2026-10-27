#pragma once

#include <cstdint>
#include <string>

/// The PCB the core is built for (the top level's g_board_issue, what NR #0F reads): Issue 2 (KS1, Spartan-6), Issue 4 (KS2,
/// Artix-7), Issue 5 (KS3, Artix-7). One VHDL for all three; zxnext.vhd generates these differences (research-variants.md):
/// - NR #11: Issue 2 is built without the VGA module (g_video_inc "10"), its PLL has VGA 0 and 1; the Artix boards' MMCM all seven
/// - NR #10: the Artix boards take a core id 0-14 only (gen_coreid_45)
/// - NR #F0 / #F8-#FA: the Artix-7's DNA and XADC (gen_xper_44)
/// - NR #04: Issue 5 keeps all 8 bits and drives SRAM A21, so config mode reaches a 4 MB SRAM's upper half (gen_romram_5, gen_sram_5)
/// - Issue 5 drives the +3 FDC lines of the expansion bus (gen_fdc_5): no card on the bus uses them here
enum class NextBoardIssue : uint8_t
{
    Issue2 = 0,
    Issue4 = 2,
    Issue5 = 3,
};
/// "issue2" / "issue4" / "issue5", also "ks1" / "ks2" / "ks3" (any case); false for anything else
bool NextBoardIssueFromName(const std::string& name, NextBoardIssue& issue);
/// The canonical name: "issue2", "issue4", "issue5"
const char* NextBoardIssueName(NextBoardIssue issue);
/// "Issue 2 (KS1)", ... for reports
const char* NextBoardIssueTitle(NextBoardIssue issue);
