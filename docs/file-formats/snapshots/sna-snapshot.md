# .SNA, .snap or .snapshot
*(Mirage Microdrive format used by many emulators)*

This format is the most well-supported of all snapshot formats (though Z80 is close on its heels) but has a drawback:

As the program counter is pushed onto the stack so that a `RETN` instruction can restart the program, 2 bytes of memory are overwritten. This will usually not matter; the game (or whatever) will have stack space that can be used for this. However, if this space is all in use when the snap is made, memory below the stack space will be corrupted. According to Rui Ribeiro, the effects of this can sometimes be avoided by replacing the corrupted bytes with zeros; e.g. take the PC from the, stack pointer, replace that word with 0000 and then increment SP. This worked with snapshots of Batman, Bounder and others which had been saved at critical points. Theoretically, this problem could cause a complete crash on a real Spectrum if the stack pointer happened to be at address 16384; the push would try and write to the ROM. How different emulators handle this is not something I know...

When the registers have been loaded, a `RETN` command is required to start the program. IFF2 is short for interrupt flip-flop 2, and for all practical purposes is the interrupt-enabled flag. Set means enabled.

**unreal-ng loader:** since `RETN` copies IFF2 into IFF1, both flip-flops are set from bit 2 of byte 19 (as libspectrum/FUSE do). The saver writes IFF2 there.

| Offset | Size | Description |
| :--- | :--- | :--- |
| 0 | 1 | byte I |
| 1 | 8 | word HL',DE',BC',AF' |
| 9 | 10 | word HL,DE,BC,IY,IX |
| 19 | 1 | byte Interrupt (bit 2 contains IFF2, 1=EI/0=DI) |
| 20 | 1 | byte R |
| 21 | 4 | words AF,SP |
| 25 | 1 | byte IntMode (0=IM0/1=IM1/2=IM2) |
| 26 | 1 | byte BorderColor (0..7, not used by Spectrum 1.7) |
| 27 | 49152 | bytes RAM dump 16384..65535 |
| **Total** | **49179** | **bytes** |

---

### .SNA (128Kb version) (SP\_EMU)

This is simply the SNA format extended to include the extra memory banks of the 128K/+2 machines, and fixes the problem with the PC being pushed onto the stack - now it is located in an extra variable in the file (and is not pushed onto the stack at all). The first 49179 bytes of the snapshot are otherwise exactly as described above, so the full description is:

| Offset | Size | Description |
| :--- | :--- | :--- |
| 0 | 27 | bytes SNA header (see above) |
| 27 | 16Kb | bytes RAM bank 5 \\ |
| 16411 | 16Kb | bytes RAM bank 2 } - as standard 48Kb SNA file |
| 32795 | 16Kb | bytes RAM bank n / (currently paged bank) |
| 49179 | 2 | word PC |
| 49181 | 1 | byte port 7FFD setting |
| 49182 | 1 | byte TR-DOS rom paged (1) or not (0) |
| 49183 | 16Kb | bytes remaining RAM banks in ascending order |
| ... | | |
| **Total** | **131103** | **or 147487 bytes** |

The third RAM bank saved is always the one currently paged, even if this is page 5 or 2 - in this case, the bank is actually included twice. The remaining RAM banks are saved in ascending order - e.g. if RAM bank 4 is paged in, the snapshot is made up of banks 5, 2 and 4 to start with, and banks 0, 1, 3, 6 and 7 afterwards. If RAM bank 5 is paged in, the snapshot is made up of banks 5, 2 and 5 again, followed by banks 0, 1, 3, 4, 6 and 7.

---

## In Unreal-NG

Snapshot overview and how loading works: [README](README.md).

**Telling the two layouts apart.** By size: exactly 49179 bytes is a 48K snapshot. A file of at least 49183 bytes whose
remainder after the first 49183 is a nonzero multiple of 16384 is a 128K snapshot: one to eight extra banks are accepted. The
standard sizes are 131103 (five extra banks) and 147487 (six: the paged bank is 5 or 2 and is stored twice). Banks a short file does not carry are not
written. Any other size is refused.

**Loading.**

- Both IFF1 and IFF2 are set from bit 2 of byte 19 (a `RETN` copies IFF2 into IFF1; libspectrum and Fuse do the same). Interrupt
  mode is taken from bits 0-1 of byte 25, the border from the low three bits of byte 26. R is loaded whole.
- **48K:** RAM goes to banks 5, 2 and 0. The PC is popped off the stack: the word at SP goes to PC and SP moves past it. When
  the stack is not in the file's RAM (SP in the ROM, or at 65535) the word is read from the machine's memory at SP, as it
  always was. The 48K BASIC ROM is selected and TR-DOS, if it was running, is ended. #7FFD is left at #10 (unlocked).
- **128K:** the third bank goes to the bank #7FFD names, #7FFD is written through the model's port decoder and then
  stored as it is (lock bit included), and the TR-DOS byte pages the TR-DOS ROM in. The PC comes from byte 49179.
- The format has no AY registers, no #1FFD and no #EFF7: the AY comes out of the reset, and models with those latches get
  their reset values (a +2A or +3 snapshot loses its special paging).
- A PC on a `HALT` opcode starts the CPU halted.

**Saving.** The 48K layout is written when #7FFD is locked, otherwise the 128K layout. The 48K saver puts the PC on the stack in
the machine's live memory before writing (two bytes under SP are overwritten in the running machine as well as in the file);
this is a known limitation of that saver.

---

## References

- [World of Spectrum FAQ: file formats](https://worldofspectrum.org/faq/reference/formats.htm), section "SNA Format" (the text of this page follows it)
- [Sinclair Wiki: SNA format](https://sinclair.wiki.zxnet.co.uk/wiki/SNA_format)
- [libspectrum: sna.c](https://github.com/speccytools/libspectrum/blob/master/sna.c), the Fuse emulator's reader and writer
- [zx-evo-docs: sna.txt](https://github.com/tslabs/zx-evo-docs/blob/main/Formats/sna.txt) (TS-Labs' notes on the format)
