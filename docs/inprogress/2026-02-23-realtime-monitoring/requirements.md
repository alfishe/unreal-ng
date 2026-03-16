Let's think about blazing fast cross-platform IPC with flexible data descriptors. We already have fixed size shared memory buffer to expose all zx-spectrum memory pages (RAM, ROM, MISC), but this approach is not scalable

Let's design a mechanism that will  be able to:
- expose data manifest - what data sections are exposed, offset, length in shared memory, other meta-information

Global information:


Per-emulator-instance information:
    State:
        - emulated memory pages (like it's already implemented)
        - emulated chips states (Z80, AY, etc.) including registers, flags, etc.
        - emulated devices states (FDC, etc.)
        - mounted tape and disk images, loaded snapshots, etc.
        - emulated video output (frame buffer, palette, etc.)
        - emulated audio buffer
        - emulated keyboard state (bi-directional)
        - emulated joystick state (bi-directional)  
        - emulated tape state
    Debugging:
        - Memory counters (read/write/execute for all addresses of all memory pages)
        - call traces
        - opcode traces
    Logging:
        - Flexible runtime reconfigured module logging. See: Modular logging system for ZX-Spectrum emulator.md


    OpenQuestions:
    - performance of such IPC
    - coherency on changes (what if external program reads from shared nemory when we're reconfiguring sections)
    - how to handle large data sections (video frame buffer, etc.)
    - how to handle binary data (audio buffer, etc.)
    - how to handle control data (keyboard, joystick, etc.)

    