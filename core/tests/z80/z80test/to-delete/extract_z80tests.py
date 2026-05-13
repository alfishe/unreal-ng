#!/usr/bin/env python3
"""
Z80Test Vector Extractor

Parses z80test-1.2a/src/tests.asm and generates C++ test vectors
for the GTest framework.

Usage:
    python3 extract_z80tests.py > z80test_vectors_generated.cpp
"""

import re
import sys

# Path to tests.asm
TESTS_ASM = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/testdata/z80/z80test-1.2a/src/tests.asm"

def parse_hex(s):
    """Parse a hex value like 0x1234 or just 1234"""
    s = s.strip()
    # Handle special tokens
    if s in ('stop', 'tail', 'mem', 'meml', 'memh', 'jmp', 'jmpl', 'jmph', 'self'):
        return 0
    if s.startswith('0x') or s.startswith('0X'):
        return int(s, 16)
    try:
        return int(s)
    except ValueError:
        return 0  # Unknown token, treat as 0

def parse_flags(line):
    """Parse flags directive like: flags s,1,z,1,f5,0,hc,1,f3,0,pv,1,n,1,c,1"""
    # Extract flag patterns
    flags_mask = 0
    # s=bit7, z=bit6, f5=bit5, hc=bit4, f3=bit3, pv=bit2, n=bit1, c=bit0
    flag_bits = {'s': 0x80, 'z': 0x40, 'f5': 0x20, 'hc': 0x10, 'f3': 0x08, 'pv': 0x04, 'n': 0x02, 'c': 0x01}
    
    # Parse comma-separated pairs
    parts = line.split(',')
    i = 0
    while i < len(parts) - 1:
        flag_name = parts[i].strip()
        flag_val = parts[i+1].strip()
        if flag_name in flag_bits and flag_val == '1':
            flags_mask |= flag_bits[flag_name]
        i += 2
    
    return flags_mask

def parse_vec(line):
    """Parse vec directive like: vec 0x37,stop,0x00,0x00,mem,0x1234,a,0xaa,f,0xff,..."""
    result = {
        'opcode': [0, 0, 0, 0],
        'mem': 0,
        'a': 0, 'f': 0,
        'bc': 0, 'de': 0, 'hl': 0,
        'ix': 0, 'iy': 0, 'sp': 0
    }
    
    parts = [p.strip() for p in line.split(',')]
    if 'a,0x09' in line:
        print(f"DEBUG: Parsing suspect line: {line}")
        print(f"DEBUG: Parts: {parts}")
    
    # First 4 values are opcodes (stop means 0 as placeholder)
    for i in range(min(4, len(parts))):
        val = parts[i]
        if val == 'stop' or val == 'tail':
            result['opcode'][i] = 0
        elif val.startswith('0x') or val.isdigit():
            result['opcode'][i] = parse_hex(val)
    
    # First pass: parse mem value so we can expand symbolic references
    i = 4
    while i < len(parts) - 1:
        name = parts[i].lower()
        val = parts[i + 1] if i + 1 < len(parts) else '0'
        if name == 'mem':
            result['mem'] = parse_hex(val)
            break
        i += 2
    
    # Track which fields should use mem value (symbolic 'mem' reference)
    use_mem_value = set()
    
    # Second pass: parse all named values
    i = 4
    while i < len(parts) - 1:
        name = parts[i].lower()
        i += 1
        if i >= len(parts):
            break
        val = parts[i]
        i += 1
        
        # Check if this is a symbolic 'mem' reference
        if val.strip().lower() == 'mem':
            if name in ('de', 'hl', 'bc', 'ix', 'iy', 'sp', 'a', 'f'):
                use_mem_value.add(name)
            continue
        
        if name == 'mem':
            result['mem'] = parse_hex(val)
        elif name == 'a':
            result['a'] = parse_hex(val)
        elif name == 'f':
            result['f'] = parse_hex(val)
        elif name == 'bc':
            result['bc'] = parse_hex(val)
        elif name == 'de':
            result['de'] = parse_hex(val)
        elif name == 'hl':
            result['hl'] = parse_hex(val)
        elif name == 'ix':
            result['ix'] = parse_hex(val)
        elif name == 'iy':
            result['iy'] = parse_hex(val)
        elif name == 'sp':
            result['sp'] = parse_hex(val)
    
    # Apply symbolic mem references
    for field in use_mem_value:
        result[field] = result['mem']
    
    return result


def parse_crcs(line):
    """Parse crcs directive like: crcs allflags,0x3ec05634,all,0xd841bd8a,..."""
    result = {
        'allflags': 0, 'all': 0,
        'docflags': 0, 'doc': 0,
        'ccf': 0, 'mptr': 0
    }
    
    parts = [p.strip() for p in line.split(',')]
    i = 0
    while i < len(parts) - 1:
        name = parts[i].lower()
        i += 1
        if i >= len(parts):
            break
        val = parts[i]
        i += 1
        
        if name in result:
            result[name] = parse_hex(val)
    
    return result

def extract_tests(asm_file):
    """Extract all test definitions from tests.asm"""
    tests = []
    
    with open(asm_file, 'r') as f:
        content = f.read()
    
    # Find all test definitions
    # Pattern: .label flags ... vec ... vec ... vec ... crcs ... name "..."
    # Split by test labels
    lines = content.split('\n')
    
    current_test = None
    vec_count = 0
    
    for line in lines:
        # Remove comments
        if ';' in line:
            line = line[:line.index(';')]
        line = line.strip()
        if not line:
            continue
        
        # Check for test label (starts with . and contains 'flags')
        if line.startswith('.') and 'flags' in line:
            # Start new test
            label_match = re.match(r'^\.(\w+)\s+flags\s+(.+)$', line)
            if label_match:
                if current_test and 'name' in current_test:
                    tests.append(current_test)
                
                label = label_match.group(1)
                flags_str = label_match.group(2)
                current_test = {
                    'label': label,
                    'flags_mask': parse_flags(flags_str),
                    'vecs': [],
                    'precheck': 255  # nocheck default
                }
                vec_count = 0
        
        # Parse vec line
        elif current_test and line.startswith('vec'):
            vec_str = line[3:].strip()
            vec = parse_vec(vec_str)
            current_test['vecs'].append(vec)
            vec_count += 1
        
        # Parse crcs line
        elif current_test and line.startswith('crcs'):
            crcs_str = line[4:].strip()
            current_test['crcs'] = parse_crcs(crcs_str)
        
        # Parse name line
        elif current_test and line.startswith('name'):
            name_match = re.search(r'"([^"]+)"', line)
            if name_match:
                current_test['name'] = name_match.group(1)
        
        # Parse precheck (failcheck, incheck, nocheck)
        elif current_test and 'failcheck' in line.lower():
            current_test['precheck'] = 0
        elif current_test and 'incheck' in line.lower() and 'db' in line.lower():
            current_test['precheck'] = 1
    
    # Don't forget last test
    if current_test and 'name' in current_test:
        tests.append(current_test)
    
    return tests

def generate_cpp(tests):
    """Generate C++ test vector array"""
    
    print("""/*
 * Z80Test Vector Data - AUTO-GENERATED
 * 
 * Extracted from z80test-1.2a/src/tests.asm
 * Reference CRCs from genuine Zilog Z80 hardware
 * 
 * DO NOT EDIT - regenerate with extract_z80tests.py
 */

#include "z80test_vectors.h"

namespace z80test {

const Z80TestVector kZ80TestVectors[] = {""")
    
    for i, test in enumerate(tests):
        if len(test.get('vecs', [])) < 3:
            continue  # Skip incomplete tests
        
        base = test['vecs'][0]
        counter = test['vecs'][1]
        shifter = test['vecs'][2]
        crcs = test.get('crcs', {})
        
        # Sanitize name for C++
        name = test.get('name', test['label']).replace('"', '\\"')
        cpp_name = test['label'].upper()
        
        print(f"    // {name}")
        print("    {")
        print(f'        .name = "{name}",')
        print(f"        .opcode = {{{hex(base['opcode'][0])}, {hex(base['opcode'][1])}, {hex(base['opcode'][2])}, {hex(base['opcode'][3])}, 0x00}},")
        print(f"        .flags_mask = {hex(test['flags_mask'])},")
        
        # Base vector
        print("        .base = {")
        print(f"            .mem = {hex(base['mem'])}, .a = {hex(base['a'])}, .f = {hex(base['f'])},")
        print(f"            .bc = {hex(base['bc'])}, .de = {hex(base['de'])}, .hl = {hex(base['hl'])},")
        print(f"            .ix = {hex(base['ix'])}, .iy = {hex(base['iy'])}, .sp = {hex(base['sp'])}")
        print("        },")
        
        # Counter vector
        print("        .counter = {")
        print(f"            .opcode = {{{hex(counter['opcode'][0])}, {hex(counter['opcode'][1])}, {hex(counter['opcode'][2])}, {hex(counter['opcode'][3])}}},")
        print(f"            .mem = {hex(counter['mem'])},")
        print(f"            .a = {hex(counter['a'])}, .f = {hex(counter['f'])},")
        print(f"            .bc = {hex(counter['bc'])}, .de = {hex(counter['de'])}, .hl = {hex(counter['hl'])},")
        print(f"            .ix = {hex(counter['ix'])}, .iy = {hex(counter['iy'])}, .sp = {hex(counter['sp'])}")
        print("        },")
        
        # Shifter vector
        print("        .shifter = {")
        print(f"            .opcode = {{{hex(shifter['opcode'][0])}, {hex(shifter['opcode'][1])}, {hex(shifter['opcode'][2])}, {hex(shifter['opcode'][3])}}},")
        print(f"            .mem = {hex(shifter['mem'])},")
        print(f"            .a = {hex(shifter['a'])}, .f = {hex(shifter['f'])},")
        print(f"            .bc = {hex(shifter['bc'])}, .de = {hex(shifter['de'])}, .hl = {hex(shifter['hl'])},")
        print(f"            .ix = {hex(shifter['ix'])}, .iy = {hex(shifter['iy'])}, .sp = {hex(shifter['sp'])}")
        print("        },")
        
        # CRCs
        print(f"        .crc_allflags = {hex(crcs.get('allflags', 0))},")
        print(f"        .crc_all = {hex(crcs.get('all', 0))},")
        print(f"        .crc_docflags = {hex(crcs.get('docflags', 0))},")
        print(f"        .crc_doc = {hex(crcs.get('doc', 0))},")
        print(f"        .crc_ccf = {hex(crcs.get('ccf', 0))},")
        print(f"        .crc_memptr = {hex(crcs.get('mptr', 0))},")
        
        # Precheck
        precheck_str = {0: 'PreCheck::FailCheck', 1: 'PreCheck::InCheck', 255: 'PreCheck::NoCheck'}
        print(f"        .precheck = {precheck_str.get(test.get('precheck', 255), 'PreCheck::NoCheck')}")
        
        if i < len(tests) - 1:
            print("    },")
        else:
            print("    }")
    
    print("""};

const size_t kZ80TestVectorCount = sizeof(kZ80TestVectors) / sizeof(kZ80TestVectors[0]);

} // namespace z80test
""")

if __name__ == '__main__':
    tests = extract_tests(TESTS_ASM)
    print(f"// Extracted {len(tests)} tests from tests.asm", file=sys.stderr)
    generate_cpp(tests)
