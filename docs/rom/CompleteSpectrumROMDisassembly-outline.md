# The Complete Spectrum ROM Disassembly

## Book Information

*   **Title**: The Complete SPECTRUM ROM DISASSEMBLY
*   **Authors**: Dr. Ian Logan & Dr. Frank O’Hara
*   **Transcription**: Transcribed by readers of the `comp.sys.sinclair` newsgroup.
*   **Publication Date**: January 1983 (original)

## Book Summary

"The Complete Spectrum ROM Disassembly" is a foundational technical manual that provides a detailed, commented disassembly of the 16-kilobyte Read-Only Memory (ROM) of the Sinclair ZX Spectrum home computer. The book meticulously breaks down the machine code that forms the Spectrum's operating system and BASIC interpreter, explaining the purpose and function of every routine. It is an essential resource for anyone wishing to understand the inner workings of the Spectrum at the lowest level, enabling advanced machine code programming, system modification, and emulation.

The book is structured into ten main sections, each focusing on a specific functional part of the ROM, from hardware interaction like keyboard scanning and cassette handling to the high-level logic of the BASIC interpreter and the floating-point calculator.

---

## Outline and Chapter Summaries

### Preface

The authors express their enjoyment in unraveling the "secrets of the Spectrum" and learning the techniques of Z80 machine code programming. They note that the Spectrum's 16K monitor program is a direct development from the earlier 4K program of the ZX80 but has been expanded to a point where the differences outweigh the similarities. They extend their thanks to their families, their publisher, Philip Mitchell for notes on the cassette format, and Clive Sinclair and his team for creating a "challenging and useful machine."

### Introduction

The introduction provides a high-level overview of the ROM's structure, dividing it into three major parts: Input/Output routines, the BASIC interpreter, and Expression handling. It then outlines the ten sections the book uses to dissect the ROM. The authors highlight key features and design choices, such as the significant improvements in cassette handling over the ZX81, the versatility of the screen and printer routines, and the trade-off of "compactness" over "speed" in the code. It also forewarns the reader of two known bugs in the arithmetic routines concerning division and the handling of the number -65536.

### 1. The Restart Routines and Tables

This chapter covers the very beginning of the ROM, from address `0000h`. It details the eight Z80 `RST` (restart) instructions, which act as fast, one-byte calls to essential system routines.

*   **Summary**: This section explains the fundamental entry points into the ROM. These routines handle critical tasks such as system startup (`RST 00h`), error reporting (`RST 08h`), printing a single character (`RST 10h`), fetching characters from the BASIC line (`RST 18h` and `RST 20h`), invoking the floating-point calculator (`RST 28h`), and handling maskable interrupts for keyboard scanning and timing (`RST 38h`). The chapter also covers the key data tables located in this area, including the **Token Table** (for converting BASIC keywords into single-byte tokens) and the **Key Tables** (for decoding keyboard presses).

### 2. The Keyboard Routines

This section describes the routines responsible for scanning the physical keyboard matrix and translating key presses into character codes.

*   **Summary**: The keyboard is scanned every 1/50th of a second (on UK models) by an interrupt routine. This chapter explains how the `KEY-SCAN` subroutine reads the hardware ports to detect which keys are pressed. It details the logic for handling single keys, shift keys (CAPS SHIFT and SYMBOL SHIFT), and decoding the presses based on the current input mode (Keyword, Letter, Extended, etc.). The system for handling automatic key repetition (`KSTATE` variables) is also explained in detail.

### 3. The Loudspeaker Routines

This chapter details how the Spectrum produces sound using its simple, single-channel internal loudspeaker.

*   **Summary**: The Spectrum generates sound by rapidly toggling a single bit on a hardware port connected to the loudspeaker. This section dissects the low-level `BEEPER` subroutine, which creates a tone of a specific frequency and duration by executing a precisely timed loop. It also covers the high-level `BEEP` command routine, which takes pitch and duration values from the calculator stack, calculates the required timing loop parameters, and then calls `BEEPER` to produce the sound. The chapter includes the 'Semi-tone' table of frequency values.

### 4. The Cassette Handling Routines

This section provides an in-depth look at one of the Spectrum's most improved features: the routines for saving and loading data from a cassette tape.

*   **Summary**: A significant portion of the ROM is dedicated to robust cassette handling. The system uses a two-part structure for saving data: a short **header block** containing information like the data type (program, array, etc.), name, and length, followed by the much larger **data block**. This chapter explains the `SA-BYTES` (Save) and `LD-BYTES` (Load) routines, detailing the format of the leader pulses, sync pulses, and the different pulse timings used to represent a '0' bit versus a '1' bit. The logic for the high-level `SAVE`, `LOAD`, `VERIFY`, and `MERGE` commands is also covered.

### 5. The Screen and Printer Handling Routines

This chapter covers all routines related to outputting text to the screen (both the main display and the lower editor area) and to an external printer.

*   **Summary**: All output is managed through a "vectored" system of channels and streams. This chapter dissects the main `PRINT-OUT` routine, which is responsible for placing a character on the screen. The routine's complexity and relative slowness are explained by its need to handle screen scrolling, cursor positioning, and attribute bytes (INK, PAPER, BRIGHT, FLASH) for every single character printed. Also covered are the powerful screen `EDITOR` for user input, the `CLS` (Clear Screen) command, and routines for handling control characters like `AT` and `TAB`.

### 6. The Executive Routines

This section describes the core "executive" part of the operating system, including the initialization sequence and the main loop that drives the BASIC interpreter.

*   **Summary**: This chapter explains what happens when the Spectrum is first powered on or reset (`INITIALISATION`). This includes memory checks, setting up system variables, and initializing I/O channels. The core of the chapter is the description of the **Main Execution Loop**. This loop calls the `EDITOR`, waits for the user to enter a line, and then checks the line's syntax. If the line begins with a number, it is saved into the program area; otherwise, it is treated as a direct command and executed immediately.

### 7. BASIC Line and Command Interpretation

This is the largest section, detailing how the Spectrum understands and executes BASIC commands.

*   **Summary**: Once a BASIC line is ready for execution, the routines in this chapter take over. A line is interpreted as a series of statements separated by colons (`:`). The interpreter finds the first command in a statement by looking up its token in the **Syntax Tables**. Based on the command's "class," the interpreter then knows what kind of parameters (e.g., a variable, a numeric expression, a string) to expect. The chapter explains this parsing process and how control is passed to the specific machine code routine that implements each BASIC command (e.g., `FOR`, `PRINT`, `GO TO`).

### 8. Expression Evaluation

This section explains the powerful but complex routines that parse and calculate the value of mathematical and string expressions.

*   **Summary**: The central routine here is `SCANNING`. It evaluates expressions by implementing a system of operator precedence (e.g., multiplication before addition). As it scans an expression, it pushes numbers (operands) onto the calculator stack and places functions and operators onto the machine stack. When an operator of lower precedence is encountered, higher-precedence operations are pulled from the stack and executed. The chapter also details the excellent dynamic string handling, which avoids the need for "garbage collection."

### 9. The Arithmetic Routines

This chapter focuses on the low-level routines that perform the four basic arithmetic operations on floating-point numbers.

*   **Summary**: The Spectrum uses two main number formats: a 'short' integer form for small whole numbers and a 5-byte floating-point form for all other numbers. This section dissects the routines for **addition, subtraction, multiplication, and division**. It explains the process of aligning exponents before addition/subtraction and normalizing the result. This chapter also provides context for the two known bugs in the ROM: a loss of precision in division and the incorrect handling of the number -65536.

### 10. The Floating-Point Calculator

This chapter describes the internal "calculator" that is at the heart of all of the Spectrum's mathematical capabilities.

*   **Summary**: The calculator operates like a stack-based machine (similar to some real-world calculators). It is invoked via a `RST 28h` instruction, which is followed by a series of literal bytes representing operations (e.g., `add`, `multiply`, `sin`). This section details these operations and, most importantly, the routines for the advanced mathematical functions (`SIN`, `LN`, `EXP`, `ATN`). These are implemented using approximations based on **Chebyshev polynomials**, which are generated by the `SERIES GENERATOR` subroutine. The code is written for compactness rather than speed.

### Appendix

The appendix provides supplementary information and algorithms that clarify some of the more complex routines in the ROM.

*   **Summary**: The appendix contains:
    1.  **BASIC programs** that simulate the series generation for `SIN`, `EXP`, `LN`, and `ATN`, making the logic behind the Chebyshev polynomial approximation easier to understand.
    2.  An explanation and BASIC simulation of the **`DRAW` algorithm**, which is used for drawing lines and arcs.
    3.  A similar explanation for the **`CIRCLE` algorithm**.
    4.  A detailed technical **note on small integers and the number -65536**, elaborating on one of the arithmetic bugs and its consequences.