# ZX Spectrum BASIC Tokenization

This document explains how **ZX Spectrum BASIC programs** are stored in memory in a *tokenized* form rather than as plain text. The description is accurate for the 48K ZX Spectrum ROM and compatible models.

---

## 1. Where BASIC programs live in memory

On a 48K ZX Spectrum, the start of the BASIC program area is pointed to by the system variable:

```
PROG = 23635 (0x5C3B)
```

Simplified memory layout:

```
[ Variables area ]  <- VARS
[ BASIC program ]  <- PROG
[ Display file ]
[ System variables ]
```

The BASIC program is stored as a contiguous sequence of tokenized lines.

---

## 2. BASIC line structure in memory

Each BASIC line is stored in the following format:

```
+----------------+----------------+--------------------+------+
| Line number    | Line length    | Tokenized content | 0x0D  |
| (2 bytes)      | (2 bytes)      | (N bytes)         | CR    |
+----------------+----------------+--------------------+------+
```

### Field details

| Field | Size | Description |
|------|------|-------------|
| Line number | 2 bytes | Big-endian (high byte first) |
| Line length | 2 bytes | Length in bytes of content **including the terminating CR (0x0D)** |
| Content | N bytes | Tokenized keywords, literals, variables |
| CR | 1 byte | End-of-line marker (0x0D) |

Example (conceptual):

```
00 0A  00 0E  F5 22 48 45 4C 4C 4F 22 0D
```

Represents:

```
10 PRINT "HELLO"
```

---

## 3. Keyword tokenization

### Token range

- BASIC keywords are replaced with **single-byte tokens**
- Tokens occupy values **0xA5–0xFF**
- Each keyword maps to exactly one byte

### Example token mappings

| Keyword | Token (hex) |
|--------|-------------|
| LET    | F1 |
| PRINT  | F5 |
| IF     | FA |
| FOR    | E4 |
| NEXT   | E5 |
| GOTO  | EC |

Example:

```
PRINT A+B
```

Stored as:

```
F5 20 41 2B 42
```

(`0x20` is a literal space)

---

## 4. Numeric constants (binary format)

Numeric literals are **not stored as text**.

Instead, each number is encoded as a **5-byte floating-point value**:

```
+------+----------------------+
| Exp  | Mantissa (4 bytes)   |
+------+----------------------+
```

- Base-2 exponent
- 32-bit mantissa
- Same format used by the ROM floating-point calculator

### Numeric marker

A byte with value `0x0E` indicates that a numeric literal follows.

Example:

```
LET A = 10
```

Stored as:

```
F1 20 41 20 3D 20 0E xx xx xx xx xx
```

Where the final 5 bytes represent the floating-point value of `10`.

---

## 5. Strings

- Strings are enclosed in quotes (`0x22`)
- Stored verbatim as ASCII bytes
- **No tokenization occurs inside strings**

Example:

```
PRINT "FOR"
```

Stored as:

```
F5 20 22 46 4F 52 22
```

The word `FOR` inside the string is not tokenized.

---

## 6. Variables

- Variable names are stored as plain characters
- Case-insensitive
- One-letter variables use one byte (`A`, `X`)
- Longer names (`TOTAL`) are stored as multiple bytes
- String variables (`A$`) and arrays are literal

Variables are resolved at runtime via the variables table, not during tokenization.

---

## 7. End of BASIC program

The end of the BASIC program is marked by a **line number of zero**:

```
00 00
```

This indicates no further lines follow.

---

## 8. Complete example

BASIC source:

```
10 LET A=10
20 PRINT A
```

Memory representation (annotated):

```
00 0A  00 0B  F1 20 41 3D 0E xx xx xx xx xx 0D
00 14  00 06  F5 20 41 0D
00 00
```

---

## 9. Why tokenization was used

### Advantages

- Very compact storage
- Fast execution on Z80
- No runtime keyword parsing
- Deterministic interpreter behavior

### Trade-offs

- Not human-readable in memory
- External editing requires detokenization
- Token values are ROM-version dependent

---

## 10. References

- *The ZX Spectrum ROM Disassembly* — Ian Logan & Frank O’Hara
- Sinclair ZX Spectrum technical manuals
- ROM BASIC interpreter source listings

---

*This format is directly applicable when parsing `.tap`, `.tzx`, or in-memory BASIC blocks, and can be described cleanly using binary pattern tools such as ImHex or Kaitai Struct.*

