#!/usr/bin/env python3
"""Co-emulation driver for SkoolKit (see README.md and ../README.md).

Loads a test program's .tap through SkoolKit's simulated LOAD "" (the ROM loader), then runs it in
SkoolKit's contention simulator (memory and I/O contention, MEMPTR, interrupts) until its DONE byte is 1,
and writes the program's memory, the final screen as text and a log:

    skoolkit-run.py --machine 48|128 --tap P.tap --done N --start N --end N --max-frames N --out OUT/48k

Exit status: 0 finished, 2 not finished or an error (the reason is the last line of OUT.log).
"""
import argparse
import contextlib
import io
import os
import sys
import tempfile
import time

TESTED_VERSIONS = ('10.1',)  # the versions this driver was run with
ACCEPTED_MAJOR = '10'        # SkoolKit's simulator API is internal: accept only this major version
SLICE_FRAMES = 50            # frames per simulator run between two looks at DONE


def fail(log, message):
    log.write(message + '\n')
    return 2


def load(machine, tap, start, snapshot):
    """Simulated LOAD "": play the tape through the ROM loader until PC = start"""
    from skoolkit import tap2sna
    with contextlib.redirect_stdout(io.StringIO()) as out:
        tap2sna.main(['-c', f'machine={machine}', '-s', str(start), tap, snapshot])
    return out.getvalue()


def make_simulator(snapshot_file):
    from skoolkit.simutils import from_snapshot
    from skoolkit.snapshot import Snapshot
    from skoolkit.trace import Tracer
    try:
        from skoolkit.ccmiosimulator import CCMIOSimulator as cls
    except ImportError:
        from skoolkit.cmiosimulator import CMIOSimulator as cls
    snapshot = Snapshot.get(snapshot_file)
    config = {'fast_djnz': False, 'fast_ldir': False}
    simulator = from_snapshot(cls, snapshot, {}, {'ay': [None] * 16}, config)
    if len(simulator.memory) == 0x20000:
        simulator.memory.out7ffd(snapshot.out7ffd)
    tracer = Tracer(simulator, snapshot.border, snapshot.out7ffd, snapshot.outfffd, list(snapshot.ay),
                    snapshot.outfe, False)
    simulator.set_tracer(tracer)
    return simulator, tracer, snapshot.pc, cls.__name__


def run_frames(simulator, tracer, pc, frames):
    """Run `frames` frames from pc, interrupts on; returns the PC it stopped at"""
    with contextlib.redirect_stdout(io.StringIO()):
        tracer.run(pc, 65536, 0, simulator.frame_duration * frames, True, None, None, None, None,
                   '$', '02X', '04X')
    return simulator.registers[24]


def screen_text(memory):
    """The screen as 24 lines of 32 characters, read back through the character set the ROM prints with"""
    chars = (memory[23606] | memory[23607] << 8) + 256
    # On the 128K, BASIC may have paged its editor ROM back in: take a character set in ROM from the 48 BASIC ROM
    font = memory.roms[1] if hasattr(memory, 'roms') and chars < 0x4000 else None
    glyphs = {}
    for code in range(32, 128):
        at = chars + (code - 32) * 8
        glyph = tuple((font[at + k] if font else memory[(at + k) & 0xFFFF]) for k in range(8))
        glyphs.setdefault(glyph, chr(code))
        glyphs.setdefault(tuple(b ^ 0xFF for b in glyph), chr(code))
    lines = []
    for row in range(24):
        line = ''
        for col in range(32):
            base = 16384 + ((row & 0x18) << 8) + ((row & 7) << 5) + col
            line += glyphs.get(tuple(memory[base + (k << 8)] for k in range(8)), ' ')
        lines.append(line.rstrip())
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser(description='SkoolKit co-emulation driver')
    p.add_argument('--machine', choices=('48', '128'), required=True)
    p.add_argument('--tap', required=True)
    p.add_argument('--done', type=int, required=True)
    p.add_argument('--start', type=int, required=True)
    p.add_argument('--end', type=int, required=True)
    p.add_argument('--max-frames', type=int, default=60000)
    p.add_argument('--out', required=True, help='output path without the extension')
    a = p.parse_args()

    with open(a.out + '.log', 'w') as log:
        try:
            import skoolkit
        except ImportError:
            return fail(log, f'{sys.executable} cannot import skoolkit')
        version = skoolkit.VERSION
        log.write(f'SkoolKit {version} ({os.path.dirname(skoolkit.__file__)}), machine {a.machine}K\n')
        if version.split('.')[0] != ACCEPTED_MAJOR:
            return fail(log, f'SkoolKit {version} is not supported: this driver uses its simulator API, tested '
                             f'with {", ".join(TESTED_VERSIONS)}')
        if version not in TESTED_VERSIONS:
            log.write(f'note: not tested with SkoolKit {version} (tested: {", ".join(TESTED_VERSIONS)})\n')

        with tempfile.TemporaryDirectory() as tmp:
            snapshot = os.path.join(tmp, 'loaded.szx')
            try:
                log.write(load(a.machine, a.tap, a.start, snapshot))
                simulator, tracer, pc, cls = make_simulator(snapshot)
            except Exception as e:  # SkoolKit's own error, e.g. a tape it cannot load
                return fail(log, f'loading failed: {e}')
        if pc != a.start:
            return fail(log, f'the loader stopped at PC #{pc:04X}, not at the start #{a.start:04X}')
        log.write(f'simulator {cls}: memory and I/O contention, MEMPTR, interrupts\n')

        memory = simulator.memory
        began = time.time()
        frames = 0
        while memory[a.done] != 1 and frames < a.max_frames:
            pc = run_frames(simulator, tracer, pc, SLICE_FRAMES)
            frames += SLICE_FRAMES
        finished = memory[a.done] == 1
        if finished:
            pc = run_frames(simulator, tracer, pc, 50)  # the closing lines
        seconds = time.time() - began
        with open(a.out + '.screen.txt', 'w') as f:
            f.write(screen_text(memory))
        if not finished:
            return fail(log, f'not done after {frames} frames (PC #{pc:04X})')
        log.write(f'DONE after about {frames} frames, {seconds:.1f} s\n')
        with open(a.out + '.bin', 'wb') as f:
            f.write(bytes(memory[x] for x in range(a.start, a.end)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
