#!/usr/bin/env python3
"""
Python port of z80test_iterator.h: counter/shifter combinatorial expansion,
plus the exact CRC-32 (IEEE 802.3 poly, init 0xFFFFFFFF, no final XOR) used
by the C++ runner and the original z80test idea.asm.
"""

VEC_SIZE = 20

_CRC32_TABLE = []


def _init_crc32_table():
    if _CRC32_TABLE:
        return
    for i in range(256):
        crc = i
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
        _CRC32_TABLE.append(crc)


_init_crc32_table()


def crc32_update(crc, byte):
    return _CRC32_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8)


class Z80TestIterator:
    def __init__(self, base, counter_mask, shifter_mask):
        self.base = list(base)
        self.counter_mask = list(counter_mask)
        self.shifter_positions = []
        for byte_idx in range(VEC_SIZE):
            mask = shifter_mask[byte_idx]
            for bit in range(8):
                if mask & (1 << bit):
                    self.shifter_positions.append((byte_idx, bit))
        self.reset()

    def reset(self):
        self.counter = list(self.counter_mask)
        self.shifter = [0] * VEC_SIZE
        self.shifter_phase = 0
        self.done = False

    def __iter__(self):
        return self

    def __next__(self):
        if self.done:
            raise StopIteration
        combined = [
            self.base[i] ^ self.counter[i] ^ self.shifter[i]
            for i in range(VEC_SIZE)
        ]
        self._advance_counter()
        return combined

    def _advance_counter(self):
        for i in range(VEC_SIZE):
            if self.counter[i] == 0:
                self.counter[i] = self.counter_mask[i]
                continue
            self.counter[i] = (self.counter[i] - 1) & self.counter_mask[i]
            return
        self._advance_shifter()

    def _advance_shifter(self):
        self.shifter_phase += 1
        if self.shifter_phase > len(self.shifter_positions):
            self.done = True
            return
        self.counter = list(self.counter_mask)
        self.shifter = [0] * VEC_SIZE
        if 0 < self.shifter_phase <= len(self.shifter_positions):
            byte_idx, bit = self.shifter_positions[self.shifter_phase - 1]
            self.shifter[byte_idx] = 1 << bit
