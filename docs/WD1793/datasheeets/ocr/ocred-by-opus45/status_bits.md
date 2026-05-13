# WD179X Datasheet: Status Register Bit Definitions

## STATUS REGISTER OVERVIEW

The Status Register (accessed at A1=0, A0=0 with RE) provides device status information. **Bit meanings change based on the last command type executed.**

| BIT | TYPE I COMMANDS | READ ADDRESS | READ SECTOR | READ TRACK | WRITE SECTOR | WRITE TRACK |
| :---: | :--- | :--- | :--- | :--- | :--- | :--- |
| **S7** | NOT READY | NOT READY | NOT READY | NOT READY | NOT READY | NOT READY |
| **S6** | WRITE PROTECT | 0 | 0 | 0 | WRITE PROTECT | WRITE PROTECT |
| **S5** | HEAD LOADED | 0 | RECORD TYPE | 0 | WRITE FAULT | WRITE FAULT |
| **S4** | SEEK ERROR | REC NOT FOUND | REC NOT FOUND | 0 | REC NOT FOUND | 0 |
| **S3** | CRC ERROR | CRC ERROR | CRC ERROR | 0 | CRC ERROR | 0 |
| **S2** | TRACK 0 | LOST DATA | LOST DATA | LOST DATA | LOST DATA | LOST DATA |
| **S1** | INDEX | DRQ | DRQ | DRQ | DRQ | DRQ |
| **S0** | BUSY | BUSY | BUSY | BUSY | BUSY | BUSY |

---

## DETAILED BIT DESCRIPTIONS

### S7 — NOT READY (All Commands)

*   **Set (1):** READY input is LOW (drive not ready)
*   **Cleared (0):** READY input is HIGH (drive ready)
*   **Effect:** If set at command start, command terminates immediately with INTRQ
*   **Notes:** Directly reflects READY pin state. Should be checked before issuing commands.

### S6 — WRITE PROTECT / Reserved

**Type I Commands:**
*   **Set (1):** WPRT input is LOW (write protection detected)
*   **Cleared (0):** WPRT input is HIGH (no write protection)
*   **Sampling:** Sampled continuously during Type I commands

**Type II/III Write Commands:**
*   **Set (1):** WPRT was active at command start → command aborted
*   **Cleared (0):** No write protection detected

**Type II/III Read Commands:**
*   **Always 0**

### S5 — HEAD LOADED / RECORD TYPE / WRITE FAULT

**Type I Commands — HEAD LOADED:**
*   **Set (1):** Head is loaded (HLD output active AND HLT input active)
*   **Cleared (0):** Head not loaded or not settled

**READ SECTOR — RECORD TYPE:**
*   **Set (1):** Deleted Data Address Mark ($F8) detected
*   **Cleared (0):** Normal Data Address Mark ($FB) detected
*   **Use:** Allows host to detect "deleted" sectors

**WRITE SECTOR / WRITE TRACK — WRITE FAULT:**
*   **Set (1):** Write Fault input (/WF) went LOW during write
*   **Cleared (0):** No write fault
*   **Effect:** Write operation terminated, data may be corrupt

**Other Commands:**
*   **Always 0**

### S4 — SEEK ERROR / RECORD NOT FOUND

**Type I Commands — SEEK ERROR:**
*   **Set (1):** Verification failed:
    - Track address in ID field doesn't match Track Register, OR
    - 5 index pulses passed without finding valid ID field, OR
    - RESTORE issued 255 step pulses without TR00 going active
*   **Cleared (0):** Verify successful or V flag was 0
*   **Also Clears:** CRC Error bit (S3)

**Type II Commands — RECORD NOT FOUND:**
*   **Set (1):** 
    - ID field with matching Track/Sector/Side not found within 5 index pulses, OR
    - Data Address Mark not found within 30 bytes (FM) / 43 bytes (MFM) of ID field
*   **Cleared (0):** Matching record found

**Type III READ ADDRESS — RECORD NOT FOUND:**
*   **Set (1):** No ID Address Mark found within 5 index pulses
*   **Cleared (0):** ID field read successfully

**Type III TRACK Commands:**
*   **Always 0**

### S3 — CRC ERROR

**Type I Commands (with V=1):**
*   **Set (1):** ID field had invalid CRC during verify
*   **Cleared (0):** All encountered ID field CRCs were valid
*   **Note:** Controller continues searching on CRC error; only the last CRC checked matters

**Type II Commands:**
*   **Set (1):** CRC error in ID field or Data field
*   **Cleared (0):** All CRCs valid

**Type III READ ADDRESS:**
*   **Set (1):** ID field CRC error
*   **Cleared (0):** ID field CRC valid

**Type III TRACK Commands:**
*   **Always 0** (no CRC checking during track-level operations)

### S2 — TRACK 0 / LOST DATA

**Type I Commands — TRACK 00:**
*   **Set (1):** TR00 input is LOW (head at Track 0)
*   **Cleared (0):** TR00 input is HIGH (not at Track 0)
*   **Sampling:** Reflects current state of TR00 pin

**Type II and III Commands — LOST DATA:**
*   **Set (1):** Host failed to service DRQ before next byte was ready
*   **Cleared (0):** All DRQs were serviced in time
*   **Write:** Previous byte was written again (data overrun)
*   **Read:** Previous byte was lost (data overrun)

### S1 — INDEX / DRQ

**Type I Commands — INDEX:**
*   **Set (1):** Index Pulse (/IP) is currently LOW
*   **Cleared (0):** No index pulse
*   **Duration:** Typically set for ~10-40 µs per revolution
*   **Use:** Can be polled to detect disk rotation or count revolutions

**Type II and III Commands — DRQ:**
*   **Set (1):** Data Register requires service:
    - Read: DR contains valid data for host to read
    - Write: DR is empty and needs new data from host
*   **Cleared (0):** Data Register does not need service
*   **Mirror:** Same state as DRQ output pin

### S0 — BUSY (All Commands)

*   **Set (1):** Command in progress
*   **Cleared (0):** Controller idle
*   **Use:** Poll to determine command completion (alternative to INTRQ)
*   **Note:** New commands (except Force Interrupt) should not be written while Busy

---

## STATUS REGISTER TIMING

| EVENT | EFFECT ON STATUS |
| :--- | :--- |
| Command written to CR | S0 (Busy) set immediately |
| Reading Status Register | INTRQ cleared |
| Command completion | S0 reset, INTRQ set |
| Force Interrupt $D0 | S0 reset, INTRQ **NOT** set |
| Force Interrupt $D8 | S0 reset, INTRQ set |
| Master Reset (/MR active) | S7 forced to 1, other bits undefined |

---

## QUICK REFERENCE BY COMMAND TYPE

### After Type I Command (Restore, Seek, Step)

```
Bit 7: NOT READY     - High = drive not ready
Bit 6: WRITE PROTECT - High = write protected
Bit 5: HEAD LOADED   - High = head engaged and settled
Bit 4: SEEK ERROR    - High = verify failed or TR00 not found
Bit 3: CRC ERROR     - High = ID CRC error during verify
Bit 2: TRACK 00      - High = at outermost track
Bit 1: INDEX         - High = index pulse currently active
Bit 0: BUSY          - High = command in progress
```

### After Type II Command (Read/Write Sector)

```
Bit 7: NOT READY     - High = drive not ready
Bit 6: WRITE PROTECT - (Write only) High = disk protected
Bit 5: RECORD TYPE   - (Read only) High = deleted data mark
       WRITE FAULT   - (Write only) High = write fault occurred
Bit 4: REC NOT FOUND - High = sector not found or no DAM
Bit 3: CRC ERROR     - High = ID or data CRC error
Bit 2: LOST DATA     - High = host failed to service DRQ
Bit 1: DRQ           - High = data register needs service
Bit 0: BUSY          - High = command in progress
```

### After Type III Command (Track Operations)

```
Bit 7: NOT READY     - High = drive not ready
Bit 6: WRITE PROTECT - (Write only) High = disk protected
Bit 5: WRITE FAULT   - (Write only) High = write fault occurred
Bit 4: REC NOT FOUND - (Read Address only) High = no ID found
Bit 3: CRC ERROR     - (Read Address only) High = ID CRC error
Bit 2: LOST DATA     - High = host failed to service DRQ
Bit 1: DRQ           - High = data register needs service
Bit 0: BUSY          - High = command in progress
```
