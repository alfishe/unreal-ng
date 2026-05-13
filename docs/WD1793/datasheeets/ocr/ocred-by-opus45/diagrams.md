# WD179X Datasheet: Flow Diagrams

This document contains comprehensive Mermaid diagram representations of the WD179X internal logic and command flows.

## INTERNAL BLOCK DIAGRAM

```mermaid
flowchart TB
    subgraph COMPUTER["COMPUTER INTERFACE"]
        DAL[DAL0-7<br/>Data Bus]
        CTRL[CS, RE, WE<br/>A0, A1]
        DRQ_OUT[DRQ]
        INTRQ_OUT[INTRQ]
    end
    
    subgraph CORE["WD179X CORE"]
        subgraph REGS["REGISTERS"]
            DR[Data Register<br/>8-bit]
            TR[Track Register<br/>8-bit]
            SR[Sector Register<br/>8-bit]
            CR[Command Register<br/>8-bit]
            STR[Status Register<br/>8-bit]
        end
        
        subgraph LOGIC["CONTROL LOGIC"]
            DSR[Data Shift Reg<br/>Serial/Parallel]
            CRC["CRC Logic<br/>G(x)=x^16+x^12+x^5+1"]
            ALU[ALU<br/>Compare/Inc/Dec]
            AM[AM Detector<br/>Address Marks]
            PLA[PLA Control<br/>230 × 16]
        end
    end
    
    subgraph DISK["FLOPPY DISK INTERFACE"]
        STEP_OUT[STEP, DIRC]
        DATA_OUT[WG, WD, EARLY, LATE]
        HEAD_OUT[HLD, TG43]
        RAW_IN[RAW READ, RCLK]
        SENSE[READY, TR00<br/>WPRT, IP, HLT]
    end
    
    DAL <--> DR
    DAL <--> TR
    DAL <--> SR
    DAL --> CR
    STR --> DAL
    CTRL --> PLA
    
    DR <--> DSR
    DSR <--> CRC
    DSR <--> AM
    TR --> ALU
    SR --> ALU
    AM --> PLA
    CRC --> STR
    ALU --> STR
    PLA --> STR
    
    PLA --> DRQ_OUT
    PLA --> INTRQ_OUT
    PLA --> STEP_OUT
    PLA --> DATA_OUT
    PLA --> HEAD_OUT
    
    RAW_IN --> DSR
    RAW_IN --> AM
    SENSE --> PLA
```

---

## HEAD LOAD TIMING DIAGRAM

```mermaid
sequenceDiagram
    participant CMD as Command
    participant HLD as HLD Output
    participant ONESHOT as External One-Shot
    participant HLT as HLT Input
    participant FDC as FDC Logic
    
    CMD->>HLD: Command with h=1 issued
    activate HLD
    Note over HLD: HLD goes HIGH
    
    HLD->>ONESHOT: Triggers external circuit
    Note over ONESHOT: 50-100ms delay<br/>(mechanical settle)
    
    ONESHOT->>HLT: Head engaged
    activate HLT
    Note over HLT: HLT goes HIGH
    
    HLT->>FDC: HLT sampled
    Note over FDC: FDC proceeds with<br/>read/write operations
    
    Note over HLD,HLT: ~2.5 seconds idle (15 revolutions)
    
    FDC->>HLD: Timeout or explicit unload
    deactivate HLD
    deactivate HLT
```

---

## TYPE I COMMAND STATE MACHINE

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> BUSY: Type I Command
    
    BUSY --> SET_STATUS: Initialize
    SET_STATUS --> CHECK_H: Set Busy, Clear errors
    
    CHECK_H --> LOAD_HEAD: h=1
    CHECK_H --> UNLOAD_HEAD: h=0
    LOAD_HEAD --> DETERMINE_CMD
    UNLOAD_HEAD --> DETERMINE_CMD
    
    DETERMINE_CMD --> RESTORE: Cmd = 0x
    DETERMINE_CMD --> SEEK: Cmd = 1x
    DETERMINE_CMD --> STEP: Cmd = 2x-7x
    
    state RESTORE {
        [*] --> CHECK_TR00
        CHECK_TR00 --> ZERO_TR: TR00 active
        CHECK_TR00 --> STEP_OUT_R: TR00 not active
        STEP_OUT_R --> WAIT_RATE
        WAIT_RATE --> INC_COUNTER
        INC_COUNTER --> CHECK_255: count++
        CHECK_255 --> SEEK_ERROR: count=255
        CHECK_255 --> CHECK_TR00: count<255
        ZERO_TR --> [*]
        SEEK_ERROR --> [*]
    }
    
    state SEEK {
        [*] --> COMPARE_TR_DR
        COMPARE_TR_DR --> DONE_SEEK: TR=DR
        COMPARE_TR_DR --> SET_DIR_S: TR≠DR
        SET_DIR_S --> UPDATE_TR_S
        UPDATE_TR_S --> ISSUE_STEP_S
        ISSUE_STEP_S --> WAIT_RATE_S
        WAIT_RATE_S --> COMPARE_TR_DR
        DONE_SEEK --> [*]
    }
    
    state STEP {
        [*] --> ISSUE_PULSE
        ISSUE_PULSE --> WAIT_RATE_ST
        WAIT_RATE_ST --> CHECK_U
        CHECK_U --> UPDATE_TR_ST: U=1
        CHECK_U --> DONE_STEP: U=0
        UPDATE_TR_ST --> DONE_STEP
        DONE_STEP --> [*]
    }
    
    RESTORE --> VERIFY_CHECK
    SEEK --> VERIFY_CHECK
    STEP --> VERIFY_CHECK
    
    VERIFY_CHECK --> COMPLETE: V=0
    VERIFY_CHECK --> VERIFY: V=1
    
    state VERIFY {
        [*] --> WAIT_15MS
        WAIT_15MS --> SAMPLE_HLT
        SAMPLE_HLT --> FIND_ID: HLT=1
        FIND_ID --> CHECK_MATCH: ID found
        FIND_ID --> TIMEOUT: 5 index pulses
        CHECK_MATCH --> CRC_CHECK: Track matches
        CHECK_MATCH --> FIND_ID: No match
        CRC_CHECK --> VERIFIED: CRC OK
        CRC_CHECK --> FIND_ID: CRC error
        TIMEOUT --> [*]
        VERIFIED --> [*]
    }
    
    VERIFY --> COMPLETE
    COMPLETE --> IDLE: INTRQ, Clear Busy
```

---

## TYPE II COMMAND STATE MACHINE

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> INIT_II: Type II Command
    
    INIT_II --> CHECK_READY: Set Busy, Clear errors
    CHECK_READY --> ABORT_NR: Not Ready
    CHECK_READY --> CHECK_WRITE: Ready
    
    CHECK_WRITE --> CHECK_WP: Write Sector
    CHECK_WRITE --> START_SEARCH: Read Sector
    CHECK_WP --> ABORT_WP: Protected
    CHECK_WP --> START_SEARCH: Not Protected
    
    START_SEARCH --> LOAD_HEAD_II: Set HLD
    LOAD_HEAD_II --> E_DELAY: E=1
    LOAD_HEAD_II --> SAMPLE_HLT: E=0
    E_DELAY --> SAMPLE_HLT: Wait 15ms
    
    SAMPLE_HLT --> SET_TG43: HLT=1
    SET_TG43 --> SEARCH_ID
    
    state SEARCH_ID {
        [*] --> WAIT_ID_AM
        WAIT_ID_AM --> TIMEOUT_II: 5 index pulses
        WAIT_ID_AM --> CHECK_TR_II: ID AM found
        CHECK_TR_II --> WAIT_ID_AM: Track mismatch
        CHECK_TR_II --> CHECK_SR: Track match
        CHECK_SR --> WAIT_ID_AM: Sector mismatch
        CHECK_SR --> CHECK_SIDE: Sector match
        CHECK_SIDE --> WAIT_ID_AM: Side mismatch (C=1)
        CHECK_SIDE --> CHECK_ID_CRC: Side OK
        CHECK_ID_CRC --> SET_CRC_II: CRC error
        SET_CRC_II --> WAIT_ID_AM
        CHECK_ID_CRC --> FOUND: CRC OK
        TIMEOUT_II --> [*]
        FOUND --> [*]
    }
    
    SEARCH_ID --> ABORT_RNF: Timeout
    SEARCH_ID --> GET_LENGTH: ID Found
    
    GET_LENGTH --> READ_DATA: Read Command
    GET_LENGTH --> WRITE_DATA: Write Command
    
    state READ_DATA {
        [*] --> WAIT_DAM
        WAIT_DAM --> DAM_TIMEOUT: No DAM
        WAIT_DAM --> RECORD_TYPE: DAM found
        RECORD_TYPE --> READ_BYTE
        READ_BYTE --> CHECK_DRQ_R
        CHECK_DRQ_R --> SET_LOST_R: DRQ still set
        CHECK_DRQ_R --> XFER_BYTE: DRQ clear
        SET_LOST_R --> XFER_BYTE
        XFER_BYTE --> SET_DRQ_R
        SET_DRQ_R --> READ_BYTE: More bytes
        SET_DRQ_R --> CHECK_CRC_R: All read
        CHECK_CRC_R --> CRC_ERR_R: Error
        CHECK_CRC_R --> DONE_R: OK
        DAM_TIMEOUT --> [*]
        CRC_ERR_R --> DONE_R
        DONE_R --> [*]
    }
    
    state WRITE_DATA {
        [*] --> GAP_DELAY
        GAP_DELAY --> SET_DRQ_W
        SET_DRQ_W --> MORE_GAP
        MORE_GAP --> CHECK_DRQ_W
        CHECK_DRQ_W --> LOST_W: DRQ still set
        CHECK_DRQ_W --> WRITE_AM: DRQ clear
        LOST_W --> [*]
        WRITE_AM --> WRITE_BYTE
        WRITE_BYTE --> SET_DRQ_W2
        SET_DRQ_W2 --> CHECK_DRQ_W2
        CHECK_DRQ_W2 --> LOST_W: DRQ still set
        CHECK_DRQ_W2 --> WRITE_BYTE: More bytes
        CHECK_DRQ_W2 --> WRITE_CRC: All written
        WRITE_CRC --> WRITE_FF
        WRITE_FF --> DONE_W
        DONE_W --> [*]
    }
    
    READ_DATA --> CHECK_M
    WRITE_DATA --> CHECK_M
    CHECK_M --> COMPLETE_II: m=0
    CHECK_M --> INC_SR: m=1
    INC_SR --> SEARCH_ID
    
    ABORT_NR --> COMPLETE_II
    ABORT_WP --> COMPLETE_II
    ABORT_RNF --> COMPLETE_II
    COMPLETE_II --> IDLE: INTRQ, Clear Busy
```

---

## FORCE INTERRUPT LOGIC

```mermaid
flowchart TD
    CMD[Force Interrupt<br/>$Dx Command] --> DECODE{I3-I0?}
    
    DECODE -->|0000| TERMINATE[Terminate Command<br/>Reset Busy<br/>NO INTRQ]
    
    DECODE -->|I0=1| COND_I0[Set Condition:<br/>Not-Ready → Ready]
    DECODE -->|I1=1| COND_I1[Set Condition:<br/>Ready → Not-Ready]
    DECODE -->|I2=1| COND_I2[Set Condition:<br/>Each Index Pulse]
    DECODE -->|I3=1| IMMEDIATE[Immediate:<br/>INTRQ Now]
    
    COND_I0 --> WAIT_COND{Condition<br/>Met?}
    COND_I1 --> WAIT_COND
    COND_I2 --> WAIT_COND
    
    WAIT_COND -->|NO| WAIT_COND
    WAIT_COND -->|YES| GEN_INTRQ[Generate INTRQ]
    
    IMMEDIATE --> GEN_INTRQ
    GEN_INTRQ --> RESET_BUSY[Reset Busy]
    TERMINATE --> DONE((IDLE))
    RESET_BUSY --> DONE
```

---

## DATA FLOW: SECTOR READ

```mermaid
sequenceDiagram
    participant HOST as Host CPU
    participant DR as Data Register
    participant DSR as Data Shift Reg
    participant CRC as CRC Logic
    participant DISK as Disk Media
    
    Note over DISK: Sector data on disk
    
    loop For each byte (128-1024)
        DISK->>DSR: Serial bits from RAW READ
        DSR->>CRC: Shift through CRC
        DSR->>DR: Parallel byte transfer
        DR->>HOST: DRQ signals byte ready
        Note over HOST: Host reads DR
        HOST->>DR: RE asserted
        Note over DR: DRQ cleared
    end
    
    DISK->>DSR: 2 bytes CRC from disk
    DSR->>CRC: Final CRC check
    
    alt CRC = 0000
        Note over CRC: CRC Valid
    else CRC ≠ 0000
        CRC->>HOST: CRC Error in Status
    end
```

---

## DATA FLOW: SECTOR WRITE

```mermaid
sequenceDiagram
    participant HOST as Host CPU
    participant DR as Data Register
    participant DSR as Data Shift Reg
    participant CRC as CRC Logic
    participant DISK as Disk Media
    
    HOST->>DR: First data byte
    Note over DR: DRQ cleared
    
    Note over DISK: Write Gate active
    DISK->>DISK: Data Address Mark written
    
    loop For each byte (128-1024)
        DR->>DSR: Parallel byte transfer
        DSR->>CRC: Shift through CRC
        DSR->>DISK: Serial bits to WD
        DR->>HOST: DRQ signals byte needed
        HOST->>DR: Next byte written
        Note over DR: DRQ cleared
    end
    
    CRC->>DSR: 16-bit CRC value
    DSR->>DISK: 2 CRC bytes written
    DSR->>DISK: 1 byte $FF written
    Note over DISK: Write Gate inactive
```
