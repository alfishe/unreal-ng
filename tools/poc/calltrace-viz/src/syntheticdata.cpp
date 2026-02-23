#include "syntheticdata.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <random>

// ==========================================================================
// loadFromFile — Load a UZVD binary dump from the emulator
// ==========================================================================
bool SyntheticData::loadFromFile(const std::string& filename)
{
    std::ifstream in(filename, std::ios::binary);
    if (!in)
        return false;

    // --- Read and validate header ---
    char magic[4];
    in.read(magic, 4);
    if (std::strncmp(magic, "UZVD", 4) != 0)
        return false;

    uint32_t version;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1)
        return false;

    // Bank mappings (4 × {mode, page})
    struct { uint8_t mode; uint8_t page; } bankMap[4];
    for (int b = 0; b < 4; b++)
    {
        in.read(reinterpret_cast<char*>(&bankMap[b].mode), 1);
        in.read(reinterpret_cast<char*>(&bankMap[b].page), 1);
    }

    // --- Raw memory: 4 × 16384 bytes ---
    in.read(reinterpret_cast<char*>(bank0_rom.data()), BANK_SIZE);
    in.read(reinterpret_cast<char*>(bank1_ram.data()), BANK_SIZE);
    in.read(reinterpret_cast<char*>(bank2_ram.data()), BANK_SIZE);
    in.read(reinterpret_cast<char*>(bank3_ram.data()), BANK_SIZE);

    // --- R/W/X counters: 65536 × uint32 each ---
    // These come as flat 64K arrays — split into 4 banks
    constexpr size_t ADDR_SPACE = 65536;
    std::vector<uint32_t> flatRead(ADDR_SPACE), flatWrite(ADDR_SPACE), flatExec(ADDR_SPACE);
    in.read(reinterpret_cast<char*>(flatRead.data()), ADDR_SPACE * sizeof(uint32_t));
    in.read(reinterpret_cast<char*>(flatWrite.data()), ADDR_SPACE * sizeof(uint32_t));
    in.read(reinterpret_cast<char*>(flatExec.data()), ADDR_SPACE * sizeof(uint32_t));

    for (int b = 0; b < 4; b++)
    {
        size_t base = b * BANK_SIZE;
        std::copy_n(flatRead.begin() + base, BANK_SIZE, readCounters[b].begin());
        std::copy_n(flatWrite.begin() + base, BANK_SIZE, writeCounters[b].begin());
        std::copy_n(flatExec.begin() + base, BANK_SIZE, execCounters[b].begin());
    }

    // --- CF data: 65536 × uint32 each for heatmap/sources/targets, then 65536 × uint8 types ---
    std::vector<uint32_t> flatCFHeat(ADDR_SPACE), flatCFSrc(ADDR_SPACE), flatCFTgt(ADDR_SPACE);
    std::vector<uint8_t> flatCFType(ADDR_SPACE);
    in.read(reinterpret_cast<char*>(flatCFHeat.data()), ADDR_SPACE * sizeof(uint32_t));
    in.read(reinterpret_cast<char*>(flatCFSrc.data()), ADDR_SPACE * sizeof(uint32_t));
    in.read(reinterpret_cast<char*>(flatCFTgt.data()), ADDR_SPACE * sizeof(uint32_t));
    in.read(reinterpret_cast<char*>(flatCFType.data()), ADDR_SPACE * sizeof(uint8_t));

    for (int b = 0; b < 4; b++)
    {
        size_t base = b * BANK_SIZE;
        std::copy_n(flatCFHeat.begin() + base, BANK_SIZE, cfHeatmap[b].begin());
        std::copy_n(flatCFSrc.begin() + base, BANK_SIZE, cfSources[b].begin());
        std::copy_n(flatCFTgt.begin() + base, BANK_SIZE, cfTargets[b].begin());

        // Map uint8_t type values to our CFType enum
        for (int i = 0; i < BANK_SIZE; i++)
        {
            uint8_t t = flatCFType[base + i];
            cfDominantType[b][i] = (t <= static_cast<uint8_t>(RETI)) ? static_cast<CFType>(t) : NONE;
        }
    }

    if (!in.good())
        return false;

    // --- Compute opcode exec count from the execute counters ---
    for (int b = 0; b < 4; b++)
        std::copy(execCounters[b].begin(), execCounters[b].end(), opcodeExecCount[b].begin());

    // Mark as loaded before deriving analytics so generators can branch on it
    isLoaded = true;

    // --- Generate derived analytics from loaded data ---
    generateEntropyMap();
    generateFreshnessMap();
    generateRegionMap();
    deriveFlowArcsFromCFData();

    // Compute per-bank stats
    for (int b = 0; b < 4; b++)
    {
        stats[b] = {};
        for (int i = 0; i < BANK_SIZE; i++)
        {
            stats[b].totalReads += readCounters[b][i];
            stats[b].totalWrites += writeCounters[b][i];
            stats[b].totalExecs += execCounters[b][i];
            if (cfHeatmap[b][i] > 0)
                stats[b].totalCFEvents += cfHeatmap[b][i];
        }
    }

    return true;
}
void SyntheticData::generate()
{
    generateMemoryContent();
    generateRWXCounters();
    generateCallTraceData();
    generateOpcodeTrace();
    generateEntropyMap();
    generateFreshnessMap();
    generateRegionMap();
    generateFlowArcs();
}

void SyntheticData::generateMemoryContent()
{
    std::mt19937 rng(42);  // Deterministic seed for reproducibility

    // Bank 0: ROM-like code (lots of opcodes, structured patterns)
    {
        auto& mem = bank0_rom;
        std::uniform_int_distribution<int> codeDist(0x00, 0xFF);

        // Fill with realistic Z80 code patterns
        for (int i = 0; i < BANK_SIZE; i++)
        {
            if (i < 0x0040)
            {
                // RST vectors: alternating JP nn patterns
                if (i % 8 == 0) mem[i] = 0xC3;      // JP nn
                else if (i % 8 == 1) mem[i] = (i / 8) * 0x10 + 0x80;
                else if (i % 8 == 2) mem[i] = 0x00;
                else mem[i] = 0x00;                  // padding
            }
            else if (i >= 0x0080 && i < 0x0400)
            {
                // Dense code region: realistic instruction mix
                int phase = i % 6;
                if (phase == 0) mem[i] = 0x3E;       // LD A,n
                else if (phase == 1) mem[i] = codeDist(rng) & 0x7F;
                else if (phase == 2) mem[i] = 0xCD;   // CALL nn
                else if (phase == 3) mem[i] = codeDist(rng);
                else if (phase == 4) mem[i] = codeDist(rng);
                else mem[i] = 0xC9;                   // RET
            }
            else if (i >= 0x1000 && i < 0x2000)
            {
                // Character set / font data: repeating byte patterns
                mem[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
            }
            else if (i >= 0x3800 && i < 0x3C00)
            {
                // Lookup tables: ascending values
                mem[i] = static_cast<uint8_t>(i & 0xFF);
            }
            else
            {
                // General code/data mix
                mem[i] = codeDist(rng);
            }
        }
    }

    // Bank 1: RAM with mixed code/data
    {
        auto& mem = bank1_ram;
        std::uniform_int_distribution<int> dist(0x00, 0xFF);
        for (int i = 0; i < BANK_SIZE; i++)
        {
            if (i >= 0x0000 && i < 0x1800)
                mem[i] = dist(rng);
            else if (i >= 0x1800 && i < 0x1B00)
                mem[i] = 0x00;  // Screen attributes (zeroed area)
            else
                mem[i] = dist(rng);
        }
    }

    // Bank 2: Mostly data (screen memory-like)
    {
        auto& mem = bank2_ram;
        for (int i = 0; i < BANK_SIZE; i++)
        {
            // ZX Spectrum screen memory pattern: lines of pixel data
            if (i < 0x1800)
            {
                int line = i / 32;
                int col = i % 32;
                mem[i] = ((line + col) % 3 == 0) ? 0xFF : 0x00;
            }
            else
            {
                // Attributes: color bytes
                int attr = (i - 0x1800) % 8;
                mem[i] = static_cast<uint8_t>(0x38 + attr);  // INK+PAPER combos
            }
        }
    }

    // Bank 3: Stack + variables
    {
        auto& mem = bank3_ram;
        std::uniform_int_distribution<int> dist(0x00, 0xFF);
        for (int i = 0; i < BANK_SIZE; i++)
        {
            if (i >= 0x3F00)
                mem[i] = dist(rng);  // Stack area: random return addresses
            else if (i < 0x0100)
                mem[i] = 0x00;       // System variables: zeroed
            else
                mem[i] = dist(rng);
        }
    }
}

void SyntheticData::generateRWXCounters()
{
    std::mt19937 rng(123);

    for (int bank = 0; bank < 4; bank++)
    {
        readCounters[bank].fill(0);
        writeCounters[bank].fill(0);
        execCounters[bank].fill(0);
    }

    // Bank 0 (ROM): heavy execute, some reads, no writes
    {
        std::uniform_int_distribution<int> noiseDist(0, 50);
        for (int i = 0; i < BANK_SIZE; i++)
        {
            // Code execution hotspots
            if (i >= 0x0080 && i < 0x0400)
                execCounters[0][i] = 500 + noiseDist(rng) * 20;
            else if (i >= 0x0000 && i < 0x0040)
                execCounters[0][i] = 2000 + noiseDist(rng) * 100;  // RST vectors very hot
            else if (i >= 0x0600 && i < 0x0800)
                execCounters[0][i] = noiseDist(rng) * 5;

            // ROM reads (table lookups)
            if (i >= 0x1000 && i < 0x2000)
                readCounters[0][i] = 100 + noiseDist(rng) * 3;
            if (i >= 0x3800 && i < 0x3C00)
                readCounters[0][i] = 200 + noiseDist(rng) * 5;
        }
    }

    // Bank 1: mixed R/W/X
    {
        std::uniform_int_distribution<int> noiseDist(0, 30);
        for (int i = 0; i < BANK_SIZE; i++)
        {
            if (i < 0x0800)
            {
                execCounters[1][i] = noiseDist(rng) * 10;
                readCounters[1][i] = noiseDist(rng) * 5;
            }
            if (i >= 0x2000 && i < 0x3000)
            {
                writeCounters[1][i] = noiseDist(rng) * 8;
                readCounters[1][i] = noiseDist(rng) * 12;
            }
        }
    }

    // Bank 2: heavy reads (screen), some writes
    {
        std::uniform_int_distribution<int> noiseDist(0, 20);
        for (int i = 0; i < BANK_SIZE; i++)
        {
            readCounters[2][i] = 50 + noiseDist(rng) * 2;
            if (i < 0x1800)
                writeCounters[2][i] = noiseDist(rng) * 3;
        }
    }

    // Bank 3: stack operations (heavy R/W near top)
    {
        std::uniform_int_distribution<int> noiseDist(0, 40);
        for (int i = 0; i < BANK_SIZE; i++)
        {
            if (i >= 0x3E00)
            {
                readCounters[3][i] = 500 + noiseDist(rng) * 30;
                writeCounters[3][i] = 500 + noiseDist(rng) * 30;
            }
            if (i < 0x0100)
                readCounters[3][i] = noiseDist(rng) * 2;
        }
    }

    // Compute stats
    for (int bank = 0; bank < 4; bank++)
    {
        for (int i = 0; i < BANK_SIZE; i++)
        {
            stats[bank].totalReads += readCounters[bank][i];
            stats[bank].totalWrites += writeCounters[bank][i];
            stats[bank].totalExecs += execCounters[bank][i];
        }
    }
}

void SyntheticData::generateCallTraceData()
{
    std::mt19937 rng(456);

    for (int bank = 0; bank < 4; bank++)
    {
        cfHeatmap[bank].fill(0);
        cfSources[bank].fill(0);
        cfTargets[bank].fill(0);
        cfDominantType[bank].fill(CFType::NONE);
    }

    // Bank 0 (ROM): Dense CF activity — entire routine ranges lit up
    {
        std::uniform_int_distribution<int> noiseDist(0, 100);

        // RST vector addresses — extremely hot targets
        uint16_t rstTargets[] = {0x0000, 0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030, 0x0038};
        for (auto addr : rstTargets)
        {
            cfTargets[0][addr] = 15000 + noiseDist(rng) * 200;
            cfHeatmap[0][addr] = cfTargets[0][addr];
            cfDominantType[0][addr] = CFType::RST;
        }

        // === Dense hot band 1: Main ROM routines 0x0040-0x0800 ===
        // This is the hot execution core — gaussian profile centered around 0x0200
        for (int i = 0x0040; i < 0x0800; i++)
        {
            float center = 0x0200;
            float dist = std::abs(static_cast<float>(i) - center);
            float sigma = 400.0f;
            float intensity = std::exp(-(dist * dist) / (2.0f * sigma * sigma));
            intensity *= 0.9f + 0.1f * (noiseDist(rng) / 100.0f);

            uint32_t hits = static_cast<uint32_t>(intensity * 8000);
            if (hits < 20) continue;

            cfHeatmap[0][i] = hits;
            cfSources[0][i] = hits / 2;
            cfTargets[0][i] = hits / 3;

            // Assign dominant types in bands
            if (i < 0x0100) cfDominantType[0][i] = CFType::RST;
            else if (i < 0x0200) cfDominantType[0][i] = CFType::CALL;
            else if (i < 0x0300) cfDominantType[0][i] = CFType::JP;
            else if (i < 0x0400) cfDominantType[0][i] = CFType::JR;
            else cfDominantType[0][i] = CFType::CALL;
        }

        // === Dense hot band 2: Character/print routines 0x0A00-0x1200 ===
        for (int i = 0x0A00; i < 0x1200; i++)
        {
            float center = 0x0D00;
            float dist = std::abs(static_cast<float>(i) - center);
            float sigma = 350.0f;
            float intensity = std::exp(-(dist * dist) / (2.0f * sigma * sigma));
            intensity *= 0.8f + 0.2f * (noiseDist(rng) / 100.0f);

            uint32_t hits = static_cast<uint32_t>(intensity * 5000);
            if (hits < 15) continue;

            cfHeatmap[0][i] += hits;
            cfSources[0][i] += hits / 3;
            cfTargets[0][i] += hits / 2;
            if (cfDominantType[0][i] == CFType::NONE)
                cfDominantType[0][i] = (i % 4 == 0) ? CFType::JR : CFType::CALL;
        }

        // === Dense hot band 3: BASIC interpreter 0x1800-0x2800 ===
        for (int i = 0x1800; i < 0x2800; i++)
        {
            float center1 = 0x1C00;
            float center2 = 0x2200;
            float dist1 = std::abs(static_cast<float>(i) - center1);
            float dist2 = std::abs(static_cast<float>(i) - center2);
            float sigma = 300.0f;
            float intensity = std::max(
                std::exp(-(dist1 * dist1) / (2.0f * sigma * sigma)),
                std::exp(-(dist2 * dist2) / (2.0f * sigma * sigma)) * 0.7f
            );
            intensity *= 0.7f + 0.3f * (noiseDist(rng) / 100.0f);

            uint32_t hits = static_cast<uint32_t>(intensity * 4000);
            if (hits < 10) continue;

            cfHeatmap[0][i] += hits;
            cfSources[0][i] += hits / 2;
            if (cfDominantType[0][i] == CFType::NONE)
                cfDominantType[0][i] = CFType::JP;
        }

        // === Scattered hot spots for visual variety ===
        for (int n = 0; n < 80; n++)
        {
            std::uniform_int_distribution<int> addrDist(0x0000, 0x3FFF);
            int addr = addrDist(rng);
            uint32_t hits = 200 + noiseDist(rng) * 30;
            cfHeatmap[0][addr] += hits;
            cfSources[0][addr] += hits / 2;
            cfDominantType[0][addr] = (n % 5 == 0) ? CFType::DJNZ :
                                      (n % 5 == 1) ? CFType::RST :
                                      (n % 5 == 2) ? CFType::RET :
                                      (n % 5 == 3) ? CFType::JP : CFType::CALL;
        }

        // DJNZ superhotspots (tight loops)
        uint16_t djnzSites[] = {0x0120, 0x0250, 0x0380, 0x0D80, 0x1C40};
        for (auto addr : djnzSites)
        {
            uint32_t hits = 40000 + noiseDist(rng) * 1000;
            cfSources[0][addr] = hits;
            cfHeatmap[0][addr] += hits;
            cfDominantType[0][addr] = CFType::DJNZ;
        }
    }

    // Bank 1: Moderate CF activity (user code)
    {
        std::uniform_int_distribution<int> noiseDist(0, 50);
        std::uniform_int_distribution<int> addrDist(0x0000, 0x0800);

        for (int n = 0; n < 25; n++)
        {
            int src = addrDist(rng);
            int tgt = addrDist(rng);
            uint32_t hits = 100 + noiseDist(rng) * 10;

            cfSources[1][src] += hits;
            cfTargets[1][tgt] += hits;
            cfHeatmap[1][src] += hits;
            cfHeatmap[1][tgt] += hits;
            cfDominantType[1][src] = CFType::CALL;
            cfDominantType[1][tgt] = CFType::CALL;
        }
    }

    // Bank 3: RET targets (stack pops returning to this bank)
    {
        std::uniform_int_distribution<int> noiseDist(0, 30);
        std::uniform_int_distribution<int> addrDist(0x0000, 0x3000);

        for (int n = 0; n < 15; n++)
        {
            int tgt = addrDist(rng);
            uint32_t hits = 50 + noiseDist(rng) * 5;
            cfTargets[3][tgt] += hits;
            cfHeatmap[3][tgt] += hits;
            cfDominantType[3][tgt] = CFType::RET;
        }
    }

    // Compute CF stats
    for (int bank = 0; bank < 4; bank++)
    {
        uint32_t maxHits = 0;
        uint16_t uniqueCount = 0;
        for (int i = 0; i < BANK_SIZE; i++)
        {
            stats[bank].totalCFEvents += cfHeatmap[bank][i];
            if (cfTargets[bank][i] > 0) uniqueCount++;
            if (cfTargets[bank][i] > maxHits) maxHits = cfTargets[bank][i];

            if (cfDominantType[bank][i] == CFType::CALL) stats[bank].callCount += cfHeatmap[bank][i];
            else if (cfDominantType[bank][i] == CFType::JP) stats[bank].jpCount += cfHeatmap[bank][i];
            else if (cfDominantType[bank][i] == CFType::JR) stats[bank].jrCount += cfHeatmap[bank][i];
            else if (cfDominantType[bank][i] == CFType::RST) stats[bank].rstCount += cfHeatmap[bank][i];
            else if (cfDominantType[bank][i] == CFType::RET) stats[bank].retCount += cfHeatmap[bank][i];
            else if (cfDominantType[bank][i] == CFType::DJNZ) stats[bank].djnzCount += cfHeatmap[bank][i];
        }
        stats[bank].uniqueTargets = uniqueCount;
        stats[bank].maxTargetHits = maxHits;
    }
}

void SyntheticData::generateOpcodeTrace()
{
    std::mt19937 rng(789);

    for (int bank = 0; bank < 4; bank++)
        opcodeExecCount[bank].fill(0);

    // Bank 0 (ROM): High execution counts at code regions
    {
        std::uniform_int_distribution<int> noiseDist(0, 100);

        // RST vectors: extremely hot
        for (int i = 0x0000; i < 0x0040; i++)
        {
            if (i % 8 < 3)  // Only the JP nn instruction bytes
                opcodeExecCount[0][i] = 10000 + noiseDist(rng) * 200;
        }

        // Main code region: variable density
        for (int i = 0x0080; i < 0x0400; i++)
        {
            int phase = i % 6;
            if (phase == 0 || phase == 2 || phase == 5)  // M1 addresses only
                opcodeExecCount[0][i] = 200 + noiseDist(rng) * 15;
        }

        // Secondary code region
        for (int i = 0x0600; i < 0x0800; i++)
        {
            if (noiseDist(rng) > 50)
                opcodeExecCount[0][i] = noiseDist(rng) * 3;
        }

        // DJNZ tight loops: super hot
        opcodeExecCount[0][0x0120] = 50000 + noiseDist(rng) * 1000;
        opcodeExecCount[0][0x0250] = 35000 + noiseDist(rng) * 800;
        opcodeExecCount[0][0x0380] = 45000 + noiseDist(rng) * 900;
    }

    // Bank 1: User code, moderate execution
    {
        std::uniform_int_distribution<int> noiseDist(0, 60);
        for (int i = 0; i < 0x0800; i++)
        {
            if (noiseDist(rng) > 30)
                opcodeExecCount[1][i] = noiseDist(rng) * 8;
        }
    }
}

void SyntheticData::generateEntropyMap()
{
    // Compute Shannon entropy over sliding 16-byte windows
    constexpr int WINDOW = 16;

    for (int bank = 0; bank < 4; bank++)
    {
        entropyMap[bank].fill(0.0f);

        const uint8_t* mem = nullptr;
        switch (bank)
        {
            case 0: mem = bank0_rom.data(); break;
            case 1: mem = bank1_ram.data(); break;
            case 2: mem = bank2_ram.data(); break;
            case 3: mem = bank3_ram.data(); break;
        }

        for (int i = 0; i < BANK_SIZE; i++)
        {
            int start = std::max(0, i - WINDOW / 2);
            int end = std::min(BANK_SIZE, i + WINDOW / 2);
            int len = end - start;

            // Count byte frequencies in window
            int freq[256] = {};
            for (int j = start; j < end; j++)
                freq[mem[j]]++;

            // Shannon entropy
            float entropy = 0.0f;
            for (int b = 0; b < 256; b++)
            {
                if (freq[b] > 0)
                {
                    float p = static_cast<float>(freq[b]) / len;
                    entropy -= p * std::log2(p);
                }
            }
            entropyMap[bank][i] = entropy;
        }
    }
}

void SyntheticData::generateFreshnessMap()
{
    for (int bank = 0; bank < 4; bank++)
        freshnessMap[bank].fill(0);

    if (isLoaded)
    {
        // ── Real data path ────────────────────────────────────────────────────
        // Derive freshness from actual write counters loaded from the .uzvd file.
        // We log-normalise per-bank write counts to [0..1] and map them onto the
        // rendering window [0..FRESH_WINDOW] so t = logNorm directly.
        //
        // currentFrame = FRESH_WINDOW, freshnessMap[i] = round(logNorm * FRESH_WINDOW)
        // → age = currentFrame - freshnessMap[i] = FRESH_WINDOW * (1 - logNorm)
        // → t  = 1 - age / FRESH_WINDOW = logNorm   (exact, no approximation)
        //
        // Cells with zero writes stay 0 (skipped by the renderer).
        constexpr uint32_t FRESH_WINDOW = 500;
        currentFrame = FRESH_WINDOW;

        for (int b = 0; b < 4; b++)
        {
            uint32_t maxW = 1;
            for (int i = 0; i < BANK_SIZE; i++)
                if (writeCounters[b][i] > maxW) maxW = writeCounters[b][i];

            const float logMax = std::log2f(static_cast<float>(maxW) + 1.0f);
            if (logMax < 1e-6f) continue;

            for (int i = 0; i < BANK_SIZE; i++)
            {
                if (writeCounters[b][i] == 0) continue;
                const float logNorm = std::log2f(static_cast<float>(writeCounters[b][i]) + 1.0f) / logMax;
                freshnessMap[b][i] = static_cast<uint32_t>(std::round(logNorm * FRESH_WINDOW));
            }
        }
        return;
    }

    // ── Synthetic data path ───────────────────────────────────────────────────
    // Used only when no real .uzvd file is loaded.
    std::mt19937 rng(999);
    currentFrame = 5000;

    // Bank 0 (ROM): Never written — stays all zeros

    // Bank 1: Some recent writes in data areas
    {
        std::uniform_int_distribution<int> frameDist(1, 5000);
        std::uniform_int_distribution<int> noiseDist(0, 100);
        for (int i = 0x2000; i < 0x3000; i++)
        {
            if (noiseDist(rng) > 40)
                freshnessMap[1][i] = frameDist(rng);
        }
        // Very recent writes in a small region (active buffer)
        for (int i = 0x2800; i < 0x2900; i++)
            freshnessMap[1][i] = currentFrame - (noiseDist(rng) % 10);
    }

    // Bank 2: Screen memory — frequently overwritten
    {
        std::uniform_int_distribution<int> noiseDist(0, 50);
        for (int i = 0; i < 0x1800; i++)
            freshnessMap[2][i] = currentFrame - noiseDist(rng);  // Very recent
        for (int i = 0x1800; i < 0x1B00; i++)
            freshnessMap[2][i] = currentFrame - 100 - noiseDist(rng);  // Attributes less recent
    }

    // Bank 3: Stack area — very active
    {
        std::uniform_int_distribution<int> noiseDist(0, 20);
        for (int i = 0x3E00; i < BANK_SIZE; i++)
            freshnessMap[3][i] = currentFrame - noiseDist(rng);
        // Variables area: sporadic writes
        std::uniform_int_distribution<int> frameDist(100, 4000);
        for (int i = 0; i < 0x0100; i++)
            freshnessMap[3][i] = frameDist(rng);
    }
}

void SyntheticData::generateRegionMap()
{
    for (int bank = 0; bank < 4; bank++)
        regionMap[bank].fill(RegionType::REG_UNKNOWN);

    // Bank 0 (ROM 48K): Well-known Spectrum ROM layout
    {
        auto& rm = regionMap[0];
        // 0x0000-0x003F: RST vectors and NMI handler (code)
        std::fill(rm.begin(), rm.begin() + 0x0040, RegionType::REG_CODE);
        // 0x0040-0x007F: System initialization code
        std::fill(rm.begin() + 0x0040, rm.begin() + 0x0080, RegionType::REG_CODE);
        // 0x0080-0x0400: Core routines (keyboard, screen, etc.)
        std::fill(rm.begin() + 0x0080, rm.begin() + 0x0400, RegionType::REG_CODE);
        // 0x0400-0x0600: Calculator routines
        std::fill(rm.begin() + 0x0400, rm.begin() + 0x0600, RegionType::REG_CALC);
        // 0x0600-0x0800: More code (BASIC interpreter)
        std::fill(rm.begin() + 0x0600, rm.begin() + 0x0800, RegionType::REG_CODE);
        // 0x0800-0x1000: Data tables and tokenization
        std::fill(rm.begin() + 0x0800, rm.begin() + 0x1000, RegionType::REG_DATA);
        // 0x1000-0x2000: Character set (8x8 font bitmaps)
        std::fill(rm.begin() + 0x1000, rm.begin() + 0x2000, RegionType::REG_SPRITE);
        // 0x2000-0x3000: BASIC interpreter continued
        std::fill(rm.begin() + 0x2000, rm.begin() + 0x3000, RegionType::REG_CODE);
        // 0x3000-0x3800: More routines
        std::fill(rm.begin() + 0x3000, rm.begin() + 0x3800, RegionType::REG_CODE);
        // 0x3800-0x3C00: Lookup tables (sin/cos, etc.)
        std::fill(rm.begin() + 0x3800, rm.begin() + 0x3C00, RegionType::REG_DATA);
        // 0x3C00-0x4000: Remainder
        std::fill(rm.begin() + 0x3C00, rm.end(), RegionType::REG_CODE);
    }

    // Bank 1 (RAM): User program with typical game layout
    {
        auto& rm = regionMap[1];
        // 0x0000-0x0800: Game code (main loop)
        std::fill(rm.begin(), rm.begin() + 0x0800, RegionType::REG_CODE);
        // 0x0800-0x0C00: Game code (subroutines)
        std::fill(rm.begin() + 0x0800, rm.begin() + 0x0C00, RegionType::REG_CODE);
        // 0x0C00-0x1000: Sound / music data
        std::fill(rm.begin() + 0x0C00, rm.begin() + 0x1000, RegionType::REG_MUSIC);
        // 0x1000-0x1800: Sprite data (UDG, game graphics)
        std::fill(rm.begin() + 0x1000, rm.begin() + 0x1800, RegionType::REG_SPRITE);
        // 0x1800-0x2000: More sprite data
        std::fill(rm.begin() + 0x1800, rm.begin() + 0x2000, RegionType::REG_SPRITE);
        // 0x2000-0x2800: Level/map data
        std::fill(rm.begin() + 0x2000, rm.begin() + 0x2800, RegionType::REG_DATA);
        // 0x2800-0x2C00: Calculation scratch area
        std::fill(rm.begin() + 0x2800, rm.begin() + 0x2C00, RegionType::REG_CALC);
        // 0x2C00-0x3000: I/O buffer (printer buffer area)
        std::fill(rm.begin() + 0x2C00, rm.begin() + 0x3000, RegionType::REG_IO_BUFFER);
        // 0x3000-0x3C00: More game data
        std::fill(rm.begin() + 0x3000, rm.begin() + 0x3C00, RegionType::REG_DATA);
        // 0x3C00-0x4000: Variables
        std::fill(rm.begin() + 0x3C00, rm.end(), RegionType::REG_SYSVARS);
    }

    // Bank 2 (RAM): Screen memory
    {
        auto& rm = regionMap[2];
        // 0x0000-0x1800: Screen pixel data
        std::fill(rm.begin(), rm.begin() + 0x1800, RegionType::REG_SCREEN);
        // 0x1800-0x1B00: Screen attributes
        std::fill(rm.begin() + 0x1800, rm.begin() + 0x1B00, RegionType::REG_SCREEN);
        // 0x1B00-0x2000: Printer buffer
        std::fill(rm.begin() + 0x1B00, rm.begin() + 0x2000, RegionType::REG_IO_BUFFER);
        // 0x2000-0x4000: BASIC program area
        std::fill(rm.begin() + 0x2000, rm.end(), RegionType::REG_CODE);
    }

    // Bank 3 (RAM): Stack + system variables
    {
        auto& rm = regionMap[3];
        // 0x0000-0x0100: System variables (5C00-5CFF equivalent)
        std::fill(rm.begin(), rm.begin() + 0x0100, RegionType::REG_SYSVARS);
        // 0x0100-0x1000: BASIC program continuation
        std::fill(rm.begin() + 0x0100, rm.begin() + 0x1000, RegionType::REG_CODE);
        // 0x1000-0x2000: Data area
        std::fill(rm.begin() + 0x1000, rm.begin() + 0x2000, RegionType::REG_DATA);
        // 0x2000-0x3000: Music player code
        std::fill(rm.begin() + 0x2000, rm.begin() + 0x2800, RegionType::REG_CODE);
        std::fill(rm.begin() + 0x2800, rm.begin() + 0x3000, RegionType::REG_MUSIC);
        // 0x3000-0x3E00: Sprite graphics
        std::fill(rm.begin() + 0x3000, rm.begin() + 0x3800, RegionType::REG_SPRITE);
        std::fill(rm.begin() + 0x3800, rm.begin() + 0x3E00, RegionType::REG_CALC);
        // 0x3E00-0x4000: Stack
        std::fill(rm.begin() + 0x3E00, rm.end(), RegionType::REG_STACK);
    }
}

void SyntheticData::generateFlowArcs()
{
    std::mt19937 rng(777);

    for (int bank = 0; bank < 4; bank++)
        flowArcs[bank].clear();

    // Bank 0 (ROM): lots of structured flows
    {
        std::uniform_int_distribution<int> noiseDist(0, 100);

        // RST calls: from scattered code sites to RST vectors
        uint16_t rstTargets[] = {0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030, 0x0038};
        for (auto tgt : rstTargets)
        {
            for (int n = 0; n < 5 + (int)(noiseDist(rng) % 8); n++)
            {
                uint16_t src = 0x0080 + (noiseDist(rng) * 37 + n * 71) % 0x0380;
                uint32_t hits = 500 + noiseDist(rng) * 50;
                flowArcs[0].push_back({src, tgt, hits, CFType::RST});
            }
        }

        // Subroutine CALL->entry and entry->RET arcs
        struct SubInfo { uint16_t entry; uint16_t retAddr; };
        SubInfo subs[] = {
            {0x0080, 0x0098}, {0x00A0, 0x00BC}, {0x00C0, 0x00E0},
            {0x0100, 0x0130}, {0x0150, 0x0180}, {0x0200, 0x0240},
            {0x0280, 0x02B0}, {0x0300, 0x0340}, {0x0350, 0x0380}
        };
        for (const auto& sub : subs)
        {
            for (int n = 0; n < 3 + (int)(noiseDist(rng) % 5); n++)
            {
                uint16_t caller = 0x0080 + (noiseDist(rng) * 23 + n * 47) % 0x0380;
                uint32_t hits = 200 + noiseDist(rng) * 20;
                flowArcs[0].push_back({caller, sub.entry, hits, CFType::CALL});
            }
            for (int n = 0; n < 3; n++)
            {
                uint16_t retTo = 0x0080 + (noiseDist(rng) * 19) % 0x0380;
                uint32_t hits = 200 + noiseDist(rng) * 15;
                flowArcs[0].push_back({sub.retAddr, retTo, hits, CFType::RET});
            }
        }

        // Short JR branches
        for (int n = 0; n < 30; n++)
        {
            uint16_t src = 0x0080 + (noiseDist(rng) * 13) % 0x0780;
            int delta = ((int)noiseDist(rng) % 60) - 20;
            uint16_t tgt = (uint16_t)std::clamp((int)src + delta, 0, BANK_SIZE - 1);
            uint32_t hits = 50 + noiseDist(rng) * 5;
            flowArcs[0].push_back({src, tgt, hits, CFType::JR});
        }

        // DJNZ tight loops
        flowArcs[0].push_back({(uint16_t)0x0120, (uint16_t)(0x0120 - 4), 30000u, CFType::DJNZ});
        flowArcs[0].push_back({(uint16_t)0x0250, (uint16_t)(0x0250 - 6), 25000u, CFType::DJNZ});
        flowArcs[0].push_back({(uint16_t)0x0380, (uint16_t)(0x0380 - 3), 40000u, CFType::DJNZ});
    }

    // Bank 1: User code flows
    {
        std::uniform_int_distribution<int> noiseDist(0, 50);
        std::uniform_int_distribution<int> addrDist(0x0000, 0x0800);

        for (int n = 0; n < 20; n++)
        {
            uint16_t src = (uint16_t)addrDist(rng);
            uint16_t tgt = (uint16_t)addrDist(rng);
            uint32_t hits = 50 + noiseDist(rng) * 8;
            CFType t = (n % 3 == 0) ? CFType::JP : (n % 3 == 1) ? CFType::CALL : CFType::JR;
            flowArcs[1].push_back({src, tgt, hits, t});
        }
    }

    // Bank 3: RET from stack 
    {
        std::uniform_int_distribution<int> noiseDist(0, 30);
        std::uniform_int_distribution<int> addrDist(0x0000, 0x2000);
        for (int n = 0; n < 10; n++)
        {
            uint16_t tgt = (uint16_t)addrDist(rng);
            uint32_t hits = 30 + noiseDist(rng) * 4;
            flowArcs[3].push_back({(uint16_t)(0x3F00 + n * 8), tgt, hits, CFType::RET});
        }
    }
}

void SyntheticData::deriveFlowArcsFromCFData()
{
    // Derive flow arcs from loaded CF source/target data by pairing
    // the top source offsets with the top target offsets within each bank.
    for (int bank = 0; bank < 4; bank++)
    {
        flowArcs[bank].clear();

        // Collect top source and target offsets (by hit count)
        struct OffsetHit { uint16_t offset; uint32_t hits; };
        std::vector<OffsetHit> sources, targets;
        sources.reserve(256);
        targets.reserve(256);

        for (int i = 0; i < BANK_SIZE; i++)
        {
            if (cfSources[bank][i] > 0)
                sources.push_back({static_cast<uint16_t>(i), cfSources[bank][i]});
            if (cfTargets[bank][i] > 0)
                targets.push_back({static_cast<uint16_t>(i), cfTargets[bank][i]});
        }

        if (sources.empty() || targets.empty())
            continue;

        // Sort by hit count descending, keep top N
        constexpr int MAX_ENDPOINTS = 64;
        auto cmpDesc = [](const OffsetHit& a, const OffsetHit& b) { return a.hits > b.hits; };
        std::sort(sources.begin(), sources.end(), cmpDesc);
        std::sort(targets.begin(), targets.end(), cmpDesc);
        if (sources.size() > MAX_ENDPOINTS) sources.resize(MAX_ENDPOINTS);
        if (targets.size() > MAX_ENDPOINTS) targets.resize(MAX_ENDPOINTS);

        // Create arcs: pair each source with nearest targets by proximity and weight
        constexpr int MAX_ARCS_PER_BANK = 200;
        int arcCount = 0;

        for (const auto& src : sources)
        {
            if (arcCount >= MAX_ARCS_PER_BANK) break;

            // Find closest targets (within reasonable distance)
            for (const auto& tgt : targets)
            {
                if (arcCount >= MAX_ARCS_PER_BANK) break;
                if (src.offset == tgt.offset) continue;

                // Weight: geometric mean of source and target hits
                uint32_t weight = static_cast<uint32_t>(
                    std::sqrt(static_cast<float>(src.hits) * static_cast<float>(tgt.hits)));
                if (weight < 10) continue;

                CFType type = cfDominantType[bank][src.offset];
                if (type == CFType::NONE) type = cfDominantType[bank][tgt.offset];
                if (type == CFType::NONE) type = CFType::JP;

                flowArcs[bank].push_back({src.offset, tgt.offset, weight, type});
                arcCount++;
            }
        }

        // Sort arcs by weight descending and trim to limit visual clutter
        std::sort(flowArcs[bank].begin(), flowArcs[bank].end(),
                  [](const FlowArc& a, const FlowArc& b) { return a.hitCount > b.hitCount; });
        if (flowArcs[bank].size() > MAX_ARCS_PER_BANK)
            flowArcs[bank].resize(MAX_ARCS_PER_BANK);
    }
}
