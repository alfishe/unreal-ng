## Track


## Sector
Each sector on an MFM track has two main parts: the **ID Field** (identifying the sector) and the **Data Field** (containing the actual sector data). The "data field" we're interested in is the part that holds the user's information (e.g., the 256 bytes of sector bytes visible to the Disk Operating System).

Here's the typical structure around the Data Field for a single sector:

```
... [Gap 2 (Post-ID Gap)] ...
+--------------------------+
| SYNC Bytes               |  (e.g., 12 bytes of 0x00)
| (Data Sync)              |
+--------------------------+
| Data Mark Preamble       |  (e.g., 3 bytes of 0xA1)
+--------------------------+
| Data Address Mark (DAM)  |  (1 byte, e.g., 0xFB for normal data, 0xF8 for deleted - though FDC handles 0xFB)
+==========================+
|                          |  <- START OF THE DATA FIELD
|       USER DATA          |  (e.g., 128, 256, 512, or 1024 MFM encoded bytes,
| (MFM Encoded Sector Data)|   representing the logical sector content)
|                          |
+==========================+  <- END OF THE DATA FIELD
| Data CRC                 |  (2 bytes, Calculated over DAM + User Data)
+--------------------------+
... [Gap 3 (Inter-Sector Gap) or Gap 4 (Pre-Index Gap)] ...
```

Let's detail the start and end:

1.  **Start of the Data Field:**
    *   The Data Field begins **immediately after** the **Data Address Mark (DAM)** byte.
    *   The DAM itself is usually preceded by three "clock" bytes (often 0xA1 in MFM, though the FDC generates these based on the data byte that follows, which is the DAM). The FDC looks for a specific clock pattern violation that the DAM byte (like 0xFB) creates when MFM encoded.
    *   So, if you see the sequence `... A1 A1 A1 FB ...` on the disk (these are the MFM encoded values), the `FB` is the Data Address Mark. The very next byte is the first byte of the MFM-encoded User Data.

2.  **End of the Data Field:**
    *   The Data Field ends **immediately before** the 2-byte **Data CRC** field.
    *   The length of the User Data portion is determined by the **Sector Size Code** found in the preceding **ID Address Mark (IDAM)** for that *same* sector.
        *   Common codes:
            *   `00` = 128 bytes per sector
            *   `01` = 256 bytes per sector (This is what TR-DOS uses)
            *   `02` = 512 bytes per sector
            *   `03` = 1024 bytes per sector
    *   So, if the Sector Size Code in the IDAM was `01` (for 256 bytes), the Data Field will contain 256 MFM-encoded bytes representing the logical sector data. After these 256 bytes, the next two bytes will be the Data CRC.

**What the "Data Field" Contains:**
The "Data Field" (the User Data part) contains the MFM-encoded representation of the logical data for that sector. For a TR-DOS disk, this would be 256 bytes of program code, file data, directory information, etc., after it has been processed by the MFM encoding rules.

**Important Distinction for CRC Calculation:**
The **Data CRC** is calculated over:
1.  The **Data Address Mark (DAM)** byte itself.
2.  All the bytes in the **User Data** field.

So, if you were to *read* the disk:
1.  Find Sync.
2.  Find Data Mark Preamble + DAM.
3.  Read the number of User Data bytes indicated by the IDAM's Sector Size Code.
4.  Read the 2 CRC bytes.
5.  To verify, calculate CRC over the (DAM + User Data bytes you just read) and compare it with the 2 CRC bytes you read from the disk.

In summary:
*   **Starts:** Right after the Data Address Mark (e.g., 0xFB).
*   **Ends:** Right before the 2-byte Data CRC.
*   **Content:** MFM-encoded user data (e.g., 256 bytes for TR-DOS).
*   **Length:** Defined by the Sector Size Code in the sector's IDAM.