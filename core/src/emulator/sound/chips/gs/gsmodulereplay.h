#pragma once

/// @file gsmodulereplay.h
/// @brief Receiving side of the module handoff on a GS-slot personality switch,
/// shared by the cards that run real firmware (classic GS, NeoGS) -
/// neogs-tdd.md §7.1.
///
/// The captured COM30 upload stream is pushed through the card's own firmware
/// the way a host loader would: wait until the firmware takes commands, open
/// the load (param first, then COM30), pace the payload one byte per firmware
/// drain, close it with D2, and restart playback with COM31 if the outgoing
/// card was playing.
///
/// The card provides (all non-virtual is fine):
///   bool     isROMLoaded() const;
///   bool     isReadyForCommands() const;   // firmware past boot, polling the mailbox
///   uint8_t  getStatusRaw() const;
///   void     sendData(uint8_t);  void sendCommand(uint8_t);  uint8_t readData();
///   void     replayAdvanceFrame();         // one ZX frame of card time
///   void     replayDrainReply();           // consume a pending card->host byte
///   void     replayRunFor(int64_t units);  // run the card this long (card units)
///   int64_t  replayByteStepUnits() const;  // pacing step while a byte is pending
///   int64_t  replayMaxByteWaitUnits() const;

#include <cstddef>
#include <cstdint>
#include <vector>

/// Capturing side of the module handoff: mirrors the raw COM30..D2 payload
/// stream at the host-port layer as the host writes it, independent of where
/// the firmware parks the bytes in card RAM. `store` holds the last stream
/// that completed with a non-empty payload; `live` is true while a stream is
/// open (mid-upload = not capturable).
struct GSUploadCapture
{
    std::vector<uint8_t> store;
    bool live = false;
    bool hadModule = false;
    bool playing = false;

    void clear()
    {
        store.clear();
        live = false;
        hadModule = false;
        playing = false;
    }

    /// Host #B3 write. The dummy slot byte precedes the OUT #BB,#30 that opens
    /// capture, so it never enters the store. Capped at the card's RAM size:
    /// the firmware has nowhere else to put more bytes, and without the cap a
    /// stream that never sends the terminating D2 would grow without bound.
    void onData(uint8_t value, size_t cap)
    {
        if (live && store.size() < cap)
            store.push_back(value);
    }

    /// Host #BB write
    void onCommand(uint8_t value)
    {
        if (value == 0x30)
        {
            store.clear();
            live = true;
        }
        else if (value == 0xD2 && live)
        {
            live = false;
            hadModule = !store.empty();
        }
        else if (value == 0x31 || value == 0x33)
        {
            playing = true;
        }
        else if (value == 0x32)
        {
            playing = false;
        }
        else if (value == 0xF3 || value == 0xF4 || value == 0x00)
        {
            // Reset wipes the firmware's module RAM: nothing left to hand off
            clear();
        }
    }

    /// Only a load that reached its D2 terminator is replayable (a switch
    /// mid-upload keeps the mailbox but loses the partial stream)
    bool capture(std::vector<uint8_t>& bytes, bool& isPlaying) const
    {
        if (live || !hadModule || store.empty())
        {
            bytes.clear();
            isPlaying = false;
            return false;
        }
        bytes = store;
        isPlaying = playing;
        return true;
    }
};

enum class GSModuleReplayResult
{
    Done,
    NothingToReplay,
    NoFirmware,
    ByteStalled, // firmware stopped draining; `stalledAt` says where
};

template <class Card>
GSModuleReplayResult gsReplayModuleUpload(Card& card, const std::vector<uint8_t>& bytes, bool startPlayback,
                                          size_t* stalledAt = nullptr)
{
    if (bytes.empty())
        return GSModuleReplayResult::NothingToReplay;
    if (!card.isROMLoaded())
        return GSModuleReplayResult::NoFirmware;

    // A healthy firmware drains a paced upload at ~700+ bytes/frame, but its
    // boot eats frames before the command loop consumes anything. The stall
    // bound turns a wedged firmware into a warning instead of a hang.
    constexpr size_t kMaxStallFrames = 1000;
    size_t stall = 0;

    // The factory hands over a constructed-but-unbooted card: the COM30 stream
    // must not interleave with the boot. Advance frames until the card reports
    // it takes commands, then settle a few frames. An already booted card skips
    // straight to the drain.
    if (!card.isReadyForCommands())
    {
        size_t boot = 0;
        while (!card.isReadyForCommands() && boot < kMaxStallFrames)
        {
            card.replayAdvanceFrame();
            boot++;
        }
        for (int i = 0; i < 4; i++)
            card.replayAdvanceFrame();
    }
    card.replayDrainReply();

    // COM30 open, the way a real loader sequences it: param first, then the
    // command; bit0 falls when the firmware dispatches. The slot reply lands in
    // the #B3 latch (its bit7 flag is consumed by the handler's own DATRG param
    // read - shared flip-flop), so drain the latch, not the flag
    card.sendData(0x01);
    card.sendCommand(0x30);
    stall = 0;
    while ((card.getStatusRaw() & 0x01) != 0 && stall < kMaxStallFrames)
    {
        card.replayAdvanceFrame();
        stall++;
    }
    card.replayAdvanceFrame();
    (void)card.readData(); // consume the slot reply latch (clears bit7 if held)

    // Paced stream, one byte per firmware drain: a #B3 write raises the pending
    // flag and the loader clears it by reading DATRG - exactly how real loaders
    // pace #B3 writes between FLAGS polls. Batching wedges the firmware's load
    // machine. Pacing is at loader granularity (a couple of interrupt periods),
    // not frame granularity: waiting a whole frame per byte froze emulation for
    // minutes on large modules (live-verified 2026-09-22).
    const int64_t byteStep = card.replayByteStepUnits();
    const int64_t maxByteWait = card.replayMaxByteWaitUnits();
    for (size_t pushed = 0; pushed < bytes.size(); pushed++)
    {
        card.sendData(bytes[pushed]);
        int64_t waited = 0;
        // bit7 falls when the loader's DATRG read consumes the byte; no host
        // #B3 read here (it would clear bit7 under the firmware)
        while ((card.getStatusRaw() & 0x80) != 0 && waited < maxByteWait)
        {
            card.replayRunFor(byteStep);
            waited += byteStep;
        }
        if (waited >= maxByteWait)
        {
            if (stalledAt)
                *stalledAt = pushed;
            return GSModuleReplayResult::ByteStalled;
        }
    }

    // D2 terminator, then a settle margin for the parse (LOAD3 posts no
    // completion reply - the command popping plus the margin is the done signal)
    card.sendCommand(0xD2);
    stall = 0;
    while ((card.getStatusRaw() & 0x01) != 0 && stall < kMaxStallFrames)
    {
        card.replayAdvanceFrame();
        stall++;
    }
    for (int i = 0; i < 8; i++)
    {
        card.replayDrainReply();
        card.replayAdvanceFrame();
    }

    // Resume playback the way the host would (DATRG 1 + COM31 starts module 1;
    // the param must be queued first - an empty DATRG read latches the last
    // uploaded module byte into the selector)
    if (startPlayback)
    {
        card.sendData(0x01);
        card.sendCommand(0x31);
        stall = 0;
        while ((card.getStatusRaw() & 0x01) != 0 && stall < kMaxStallFrames)
        {
            card.replayAdvanceFrame();
            stall++;
        }
        for (int i = 0; i < 3; i++)
            card.replayAdvanceFrame();
        (void)card.readData(); // COM31 status reply: latch drain (flag may be gone)
    }
    return GSModuleReplayResult::Done;
}
