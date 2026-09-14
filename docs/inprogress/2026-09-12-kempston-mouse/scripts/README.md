# Kempston Mouse Debug Scripts

Test and debug scripts for Kempston mouse emulation.

## Scripts

### service_monitor_test.py

Automated test for mouse detection via Scorpion ProfROM Service Monitor.

**Prerequisites:**
- Emulator running with Scorpion + ProfROM configuration
- WebAPI enabled on port 8090

**Usage:**
```bash
python3 service_monitor_test.py
```

**What it does:**
1. Enables port trace profiler
2. Triggers Magic NMI to enter Service Monitor
3. Waits for hardware probes to complete
4. Reads E03B control byte (bit 5 = mouse detected)
5. Reports mouse port events from port trace

**Expected output (mouse working):**
```
E03B after NMI: 0xE8
  Mouse detected: True
  Joystick detected: True
Mouse port events (0x??DF): 3+
  0xFADF (buttons), 0xFBDF (X), 0xFFDF (Y)
```
