#
# IDA Pro Python Script for Analyzing ZX Spectrum ROM/RAM pages Access Counts
#
# This script reads a YAML file containing memory access counters (reads, writes, executes)
# for each address in the ZX Spectrum ROM or RAM page. It then uses this data to guide
# the disassembly process in IDA Pro.
# Expected input:
# page: "ROM 3"
#   accessed_addresses:
#     0x0010: {reads: 0, writes: 0, executes: 176}
#     0x0011: {reads: 0, writes: 0, executes: 176}
#     0x0012: {reads: 0, writes: 0, executes: 176}
#     0x0038: {reads: 0, writes: 0, executes: 51}
#     0x0039: {reads: 0, writes: 0, executes: 51}
#     0x003A: {reads: 0, writes: 0, executes: 51}
#     0x003B: {reads: 0, writes: 0, executes: 51}
#
# The script performs the following steps:
# 1. Prompts the user to select the YAML input file.
# 2. Parses the file to extract access counts for each address.
# 3. Identifies contiguous code segments based on the 'executes' counter.
# 4. Pass 1 (Code Definition): Iterates through each identified code segment, undefines it,
#    and then creates instructions sequentially through the entire block.
# 5. Pass 2 (Data Definition & Comments): Iterates through all addresses again.
#    - It adds the access counts as a comment to every address listed in the file.
#    - If an address was only read/written (zero 'executes') and is currently
#      undefined, it is defined as a data byte (`db`).
# 6. Pass 3 (Data Coalescing): Scans for adjacent data bytes that have identical, non-zero
#    read counts and attempts to combine them into data words (`dw`).
#
# To use:
# 1. Run the script in IDA Pro (File -> Script file...).
# 2. Select memory counters yaml file when prompted (for example  `ROM_3.yaml`).
# 3. The script will output its progress to the IDA Output window.
#
# Note: This script performs a simple parse of the YAML file and does not require
#       an external PyYAML library.
#

import ida_kernwin
import ida_bytes
import ida_ua
import re

def parse_access_file(filepath):
    """
    Parses the YAML-like file to extract address access counts.
    Returns a dictionary mapping address -> {'r': count, 'w': count, 'e': count}.
    """
    access_data = {}
    in_addresses_section = False
    
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                
                if line.startswith('accessed_addresses:'):
                    in_addresses_section = True
                    continue

                if not in_addresses_section:
                    continue

                # Use regex to parse '0xADDR: {reads: R, writes: W, executes: E}'
                match = re.search(r"^\s*(0x[0-9a-fA-F]+):\s*\{reads:\s*(\d+),\s*writes:\s*(\d+),\s*executes:\s*(\d+)\}", line)
                if match:
                    addr_str, r_str, w_str, e_str = match.groups()
                    address = int(addr_str, 16)
                    r = int(r_str)
                    w = int(w_str)
                    e = int(e_str)
                    access_data[address] = {'r': r, 'w': w, 'e': e}
                elif not line.startswith('page:'): # Skip known metadata lines, warn on others
                    print(f"Warning: Could not parse line: {line}")

    except IOError as e:
        ida_kernwin.warning(f"Could not read file: {e}")
        return None
        
    return access_data

def find_code_segments(access_data):
    """
    Analyzes access data to find contiguous blocks of executed code.
    Returns a list of (start, end) tuples for each code segment.
    """
    # Get all addresses with a non-zero execute count
    code_addrs = sorted([addr for addr, counts in access_data.items() if counts['e'] > 0])
    
    if not code_addrs:
        return []

    segments = []
    seg_start = code_addrs[0]
    
    for i in range(1, len(code_addrs)):
        # If the current address is not contiguous with the previous one, end the segment
        if code_addrs[i] != code_addrs[i-1] + 1:
            segments.append((seg_start, code_addrs[i-1]))
            seg_start = code_addrs[i]
            
    # Add the last segment
    segments.append((seg_start, code_addrs[-1]))
    
    return segments


def main():
    """Main function to run the analysis."""
    # 1. Get file path from user
    filepath = ida_kernwin.ask_file(0, "*.yaml;*.txt", "Select the memory access YAML file")
    if not filepath:
        print("Analysis cancelled: No file selected.")
        return

    # 2. Parse the file
    print(f"Parsing access data from {filepath}...")
    access_data = parse_access_file(filepath)
    if not access_data:
        print("Analysis failed: Could not parse the file.")
        return
        
    print(f"Successfully parsed {len(access_data)} addresses.")

    # Find all contiguous code segments first
    print("\nIdentifying code segments...")
    code_segments = find_code_segments(access_data)
    print(f"Found {len(code_segments)} code segments.")
    for start, end in code_segments:
        print(f"  -> Code segment: {start:#06x} - {end:#06x}")

    # --- Pass 1: Define Code in Segments ---
    # This pass iterates through the identified segments and defines instructions.
    print("\n--- Pass 1: Defining Code from Segments ---")
    total_insn_count = 0
    for start, end in code_segments:
        # Undefine the entire range to clear any existing definitions
        ida_bytes.del_items(start, ida_bytes.DELIT_SIMPLE, end - start + 1)
        
        current_addr = start
        while current_addr <= end:
            if not ida_bytes.is_loaded(current_addr):
                break
            
            if ida_ua.create_insn(current_addr):
                total_insn_count += 1
                # Move to the next instruction
                insn_size = ida_bytes.get_item_size(current_addr)
                current_addr += max(1, insn_size) # max(1,...) to prevent infinite loops on failure
            else:
                # If we fail to create an instruction, mark as byte and move on
                print(f"Warning: Failed to create instruction at {current_addr:#06x}. Marking as data.")
                ida_bytes.create_byte(current_addr, 1)
                current_addr += 1

    print(f"Defined {total_insn_count} instructions across all segments.")
    
    # --- Pass 2: Define Data and Add Comments ---
    # This pass adds comments and defines simple data bytes for addresses
    # that were only read/written and are not already part of an instruction.
    print("\n--- Pass 2: Defining Data and Adding Comments ---")
    data_count = 0
    comment_count = 0
    sorted_addresses = sorted(access_data.keys())

    for address in sorted_addresses:
        if not ida_bytes.is_loaded(address):
            continue
            
        counts = access_data[address]
        r, w, e = counts['r'], counts['w'], counts['e']
        
        # Add a comment with the access counts
        comment = f"R={r}, W={w}, E={e}"
        ida_bytes.set_cmt(address, comment, 0)
        comment_count += 1
        
        # If not executed, but read/written, and is currently undefined,
        # mark it as a data byte.
        if e == 0 and (r > 0 or w > 0):
            flags = ida_bytes.get_flags(address)
            if ida_bytes.is_unknown(flags):
                ida_bytes.create_byte(address, 1)
                data_count += 1
                
    print(f"Added {comment_count} access count comments.")
    print(f"Defined {data_count} bytes as data for non-executed locations.")
    
    # --- Pass 3: Coalesce Data Bytes into Words ---
    # This pass heuristically combines adjacent bytes into words if their
    # read patterns are identical, suggesting a 16-bit data read.
    print("\n--- Pass 3: Coalescing Data Bytes into Words ---")
    word_count = 0
    addr_idx = 0
    addrs_in_file = sorted([addr for addr, c in access_data.items() if c['e'] == 0])

    while addr_idx < len(addrs_in_file) - 1:
        addr1 = addrs_in_file[addr_idx]
        addr2 = addrs_in_file[addr_idx + 1]

        if not (ida_bytes.is_loaded(addr1) and ida_bytes.is_loaded(addr2)):
            addr_idx += 1
            continue

        # Check if addresses are consecutive
        if addr2 != addr1 + 1:
            addr_idx += 1
            continue

        counts1 = access_data.get(addr1)
        counts2 = access_data.get(addr2)
        
        # Heuristic: both are pure data, have same non-zero read count
        if (counts1 and counts2 and
            counts1['e'] == 0 and counts2['e'] == 0 and
            counts1['r'] > 0 and counts1['r'] == counts2['r']):
            
            # Check if they are currently defined as single bytes in IDA
            flags1 = ida_bytes.get_flags(addr1)
            flags2 = ida_bytes.get_flags(addr2)
            if ida_bytes.is_byte(flags1) and ida_bytes.is_byte(flags2):
                ida_bytes.del_items(addr1, 0, 2)
                if ida_bytes.create_word(addr1, 2):
                    word_count += 1
                    addr_idx += 2  # Skip the next address since we formed a word
                    continue
                else:
                    # If creation fails, restore them as bytes to be safe
                    ida_bytes.create_byte(addr1, 1)
                    ida_bytes.create_byte(addr2, 1)
                    print(f"Warning: Failed to create word at {addr1:#06x}, restored bytes.")

        addr_idx += 1

    print(f"Coalesced {word_count} pairs of bytes into words.")
    
    print("\n---- Analysis Complete ----")
    ida_kernwin.info("Analysis based on access counts is complete!")


if __name__ == "__main__":
    main()