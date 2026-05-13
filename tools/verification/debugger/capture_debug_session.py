#!/usr/bin/env python3
"""
Comprehensive Debug Session Capture Script

Connects to emulator WebAPI, captures multiple debug data types:
- Opcode profiler counters and trace
- CPU registers
- Memory counters  
- Call trace
- Stack area
- Disassembly at current PC

Usage:
    python capture_debug_session.py [--host HOST] [--port PORT]
    
The script will:
1. Connect to emulator WebAPI
2. Enable opcode profiler and start capture
3. Wait for user keypress (manual operation time)
4. Capture all debug data
5. Save to timestamped results folder

Output files in results/YYYY-MM-DD-HH-MM-debugcapture/:
    - session_metadata.yaml     - Session info
    - registers.yaml            - CPU register state
    - opcode_counters.yaml      - Opcode execution counts
    - opcode_trace.yaml         - Recent execution trace
    - memcounters.yaml          - Memory access statistics
    - calltrace.yaml            - Call stack history
    - stack_memory.yaml         - Stack area dump
    - disasm_at_pc.yaml         - Disassembly at current PC
"""

import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

try:
    import requests
except ImportError:
    print("ERROR: 'requests' library not found.")
    print("Please install it with: pip install requests")
    sys.exit(1)

try:
    import yaml
except ImportError:
    print("ERROR: 'pyyaml' library not found.")
    print("Please install it with: pip install pyyaml")
    sys.exit(1)


class Colors:
    """ANSI color codes for terminal output"""
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    BOLD = '\033[1m'
    END = '\033[0m'


class DebugCapture:
    """Comprehensive debug capture harness via WebAPI"""
    
    def __init__(self, host: str = "localhost", port: int = 8090):
        self.base_url = f"http://{host}:{port}"
        self.emulator_uuid: Optional[str] = None
        self.session = requests.Session()
        self.session.headers.update({'Content-Type': 'application/json'})
        
        self.session_start: Optional[datetime] = None
        self.results_dir: Optional[Path] = None
        
    def print_success(self, message: str):
        print(f"{Colors.GREEN}✓{Colors.END} {message}")
        
    def print_error(self, message: str):
        print(f"{Colors.RED}✗{Colors.END} {message}")
        
    def print_info(self, message: str):
        print(f"{Colors.BLUE}ℹ{Colors.END} {message}")
        
    def print_header(self, message: str):
        print(f"\n{Colors.BOLD}{message}{Colors.END}")
        
    def check_connection(self) -> bool:
        """Check if WebAPI is accessible"""
        self.print_header("Checking WebAPI connection...")
        try:
            response = self.session.get(f"{self.base_url}/api/v1/emulator", timeout=5)
            if response.status_code == 200:
                self.print_success(f"WebAPI accessible at {self.base_url}")
                return True
            else:
                self.print_error(f"WebAPI returned status code {response.status_code}")
                return False
        except requests.exceptions.ConnectionError:
            self.print_error(f"Cannot connect to WebAPI at {self.base_url}")
            return False
        except Exception as e:
            self.print_error(f"Unexpected error: {e}")
            return False
            
    def discover_emulators(self) -> list:
        """Discover running emulator instances"""
        try:
            response = self.session.get(f"{self.base_url}/api/v1/emulator", timeout=5)
            response.raise_for_status()
            data = response.json()
            return data.get('emulators', [])
        except Exception as e:
            self.print_error(f"Failed to discover emulators: {e}")
            return []
            
    def select_emulator(self, uuid: Optional[str] = None) -> bool:
        """Select an emulator instance"""
        self.print_header("Selecting emulator...")
        
        if uuid:
            self.emulator_uuid = uuid
            self.print_info(f"Using specified emulator: {uuid[:8]}...")
            return True
            
        emulators = self.discover_emulators()
        if not emulators:
            self.print_error("No running emulator instances found")
            return False
            
        self.emulator_uuid = emulators[0]['id']
        self.print_success(f"Auto-selected emulator: {self.emulator_uuid[:8]}...")
        return True
        
    def start_profilers(self) -> bool:
        """Start all profiler capture sessions individually"""
        self.print_header("Starting profiler sessions...")
        self.session_start = datetime.now()
        
        success = True
        
        # Start opcode profiler
        if self._profiler_control("opcode", "start"):
            self.print_success("Opcode profiler started")
        else:
            self.print_error("Failed to start opcode profiler")
            success = False
            
        # Start memory profiler
        if self._profiler_control("memory", "start"):
            self.print_success("Memory profiler started")
        else:
            self.print_error("Failed to start memory profiler")
            success = False
            
        # Start calltrace profiler
        if self._profiler_control("calltrace", "start"):
            self.print_success("Call trace profiler started")
        else:
            self.print_error("Failed to start call trace profiler")
            success = False
            
        return success
        
    def stop_profilers(self) -> bool:
        """Stop all profiler capture sessions individually"""
        self.print_header("Stopping profiler sessions...")
        
        # Stop each profiler
        self._profiler_control("opcode", "stop")
        self._profiler_control("memory", "stop")
        self._profiler_control("calltrace", "stop")
        
        self.print_success("All profilers stopped")
        return True
        
    def _profiler_control(self, profiler: str, action: str) -> bool:
        """Control a specific profiler session via new individual endpoints"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/profiler/{profiler}/{action}"
        
        try:
            response = self.session.post(url, timeout=5)
            response.raise_for_status()
            return True
        except Exception as e:
            return False
    
    # === Opcode Profiler Control ===
    def start_opcode_profiler(self) -> bool:
        return self._profiler_control("opcode", "start")
        
    def stop_opcode_profiler(self) -> bool:
        return self._profiler_control("opcode", "stop")
        
    def pause_opcode_profiler(self) -> bool:
        return self._profiler_control("opcode", "pause")
        
    def resume_opcode_profiler(self) -> bool:
        return self._profiler_control("opcode", "resume")
        
    def clear_opcode_profiler(self) -> bool:
        return self._profiler_control("opcode", "clear")
    
    # === Memory Profiler Control ===
    def start_memory_profiler(self) -> bool:
        return self._profiler_control("memory", "start")
        
    def stop_memory_profiler(self) -> bool:
        return self._profiler_control("memory", "stop")
        
    def pause_memory_profiler(self) -> bool:
        return self._profiler_control("memory", "pause")
        
    def resume_memory_profiler(self) -> bool:
        return self._profiler_control("memory", "resume")
        
    def clear_memory_profiler(self) -> bool:
        return self._profiler_control("memory", "clear")
    
    # === Call Trace Profiler Control ===
    def start_calltrace_profiler(self) -> bool:
        return self._profiler_control("calltrace", "start")
        
    def stop_calltrace_profiler(self) -> bool:
        return self._profiler_control("calltrace", "stop")
        
    def pause_calltrace_profiler(self) -> bool:
        return self._profiler_control("calltrace", "pause")
        
    def resume_calltrace_profiler(self) -> bool:
        return self._profiler_control("calltrace", "resume")
        
    def clear_calltrace_profiler(self) -> bool:
        return self._profiler_control("calltrace", "clear")

    def api_get(self, endpoint: str, params: dict = None) -> dict:
        """Generic GET request to emulator API"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/{endpoint}"
        try:
            response = self.session.get(url, params=params, timeout=10)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            self.print_error(f"Failed to get {endpoint}: {e}")
            return {}
            
    def get_registers(self) -> dict:
        """Get CPU registers"""
        return self.api_get("registers")
        
    def get_opcode_counters(self, limit: int = 100) -> dict:
        """Get opcode execution counters"""
        return self.api_get("profiler/opcode/counters", {"limit": limit})
        
    def get_opcode_trace(self, count: int = 500) -> dict:
        """Get recent execution trace"""
        return self.api_get("profiler/opcode/trace", {"count": count})
        
    def get_memcounters(self) -> dict:
        """Get memory access statistics"""
        return self.api_get("memcounters")
        
    def get_calltrace(self, limit: int = 100) -> dict:
        """Get call trace history (legacy endpoint)"""
        return self.api_get("calltrace", {"limit": limit})
        
    def get_calltrace_entries(self, count: int = 200) -> dict:
        """Get call trace profiler entries via new API"""
        return self.api_get("profiler/calltrace/entries", {"count": count})
        
    def get_calltrace_stats(self) -> dict:
        """Get call trace profiler statistics"""
        return self.api_get("profiler/calltrace/stats")
        
    def get_memory_profiler_pages(self, limit: int = 50) -> dict:
        """Get memory profiler per-page access summaries"""
        return self.api_get("profiler/memory/pages", {"limit": limit})
        
    def get_memory_profiler_status(self) -> dict:
        """Get memory profiler status"""
        return self.api_get("profiler/memory/status")
        
    def get_profiler_status(self) -> dict:
        """Get unified profiler status (all profilers)"""
        return self.api_get("profiler/status")
        
    def get_stack_memory(self, sp: int, size: int = 64) -> dict:
        """Get memory around stack pointer"""
        # Read from SP - 16 to SP + 48 to see context
        start = max(0, sp - 16)
        return self.api_get(f"memory/{start:04X}", {"len": size})
        
    def get_disasm(self, address: int = None, count: int = 20) -> dict:
        """Get disassembly at address"""
        params = {"count": count}
        if address is not None:
            params["address"] = address
        return self.api_get("disasm", params)
        
    def create_results_dir(self) -> Path:
        """Create timestamped results directory"""
        timestamp = datetime.now().strftime("%Y-%m-%d-%H-%M-debugcapture")
        results_dir = Path(__file__).parent / "results" / timestamp
        results_dir.mkdir(parents=True, exist_ok=True)
        return results_dir
        
    def save_yaml(self, data: dict, filename: str, description: str = None):
        """Save data to YAML file"""
        if not data:
            return
            
        filepath = self.results_dir / filename
        with open(filepath, 'w') as f:
            yaml.dump(data, f, default_flow_style=False, sort_keys=False)
        
        desc = f" ({description})" if description else ""
        self.print_success(f"Saved: {filename}{desc}")
        
    def dump_session_data(self):
        """Dump all session data to files"""
        self.print_header("Capturing debug data...")
        
        self.results_dir = self.create_results_dir()
        
        # Session metadata
        session_end = datetime.now()
        duration = (session_end - self.session_start).total_seconds() if self.session_start else 0
        
        metadata = {
            "session_start": self.session_start.isoformat() if self.session_start else None,
            "session_end": session_end.isoformat(),
            "duration_seconds": duration,
            "api_host": self.base_url,
            "emulator_id": self.emulator_uuid,
            "capture_type": "debug_comprehensive"
        }
        self.save_yaml(metadata, "session_metadata.yaml")
        
        # Get unified profiler status
        profiler_status = self.get_profiler_status()
        self.save_yaml(profiler_status, "profiler_status.yaml", "all profilers status")
        
        # Get registers first (need SP for stack dump)
        registers = self.get_registers()
        self.save_yaml(registers, "registers.yaml", "CPU state")
        
        # === OPCODE PROFILER ===
        self.print_header("Opcode Profiler data...")
        counters = self.get_opcode_counters(100)
        total = counters.get('total_executions', 0)
        self.save_yaml(counters, "opcode_counters.yaml", f"{total:,} executions")
        
        trace = self.get_opcode_trace(500)
        count = trace.get('returned_count', 0)
        self.save_yaml(trace, "opcode_trace.yaml", f"{count} entries")
        
        # === MEMORY PROFILER ===
        self.print_header("Memory Profiler data...")
        mem_status = self.get_memory_profiler_status()
        self.save_yaml(mem_status, "memory_profiler_status.yaml", "memory profiler status")
        
        mem_pages = self.get_memory_profiler_pages(100)
        page_count = len(mem_pages.get('pages', []))
        self.save_yaml(mem_pages, "memory_pages.yaml", f"{page_count} pages")
        
        # Legacy memcounters endpoint
        memcounters = self.get_memcounters()
        self.save_yaml(memcounters, "memcounters.yaml", "memory access stats")
        
        # === CALL TRACE PROFILER ===
        self.print_header("Call Trace Profiler data...")
        calltrace_entries = self.get_calltrace_entries(200)
        ct_count = calltrace_entries.get('returned_count', 0)
        self.save_yaml(calltrace_entries, "calltrace_entries.yaml", f"{ct_count} entries")
        
        calltrace_stats = self.get_calltrace_stats()
        self.save_yaml(calltrace_stats, "calltrace_stats.yaml", "call/return statistics")
        
        # Legacy calltrace endpoint
        calltrace = self.get_calltrace(100)
        entries = len(calltrace.get('call_trace', []))
        self.save_yaml(calltrace, "calltrace_legacy.yaml", f"{entries} call entries")
        
        # === GENERAL DEBUG DATA ===
        self.print_header("General debug data...")
        
        # Stack memory (if we have SP)
        if registers and 'registers' in registers:
            sp = registers['registers'].get('sp', 0)
            if sp > 0:
                stack = self.get_stack_memory(sp, 128)
                self.save_yaml(stack, "stack_memory.yaml", f"SP=0x{sp:04X}")
        
        # Disassembly at current PC
        disasm = self.get_disasm(count=30)
        self.save_yaml(disasm, "disasm_at_pc.yaml", "current PC")
        
        self.print_info(f"Results saved to: {self.results_dir}")
        
    def run(self, emulator_uuid: Optional[str] = None) -> bool:
        """Run the capture session"""
        
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.BOLD}Comprehensive Debug Session Capture{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}")
        
        # Connect and setup
        if not self.check_connection():
            return False
            
        if not self.select_emulator(emulator_uuid):
            return False
            
        # Start all profilers individually
        if not self.start_profilers():
            self.print_error("Some profilers failed to start, continuing anyway...")
            
        # Wait for user
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.GREEN}All profilers capturing!{Colors.END}")
        print(f"{Colors.BOLD}Run your Z80 code until the bug occurs.{Colors.END}")
        print(f"{Colors.CYAN}This will capture:{Colors.END}")
        print(f"  - Opcode execution counts and trace")
        print(f"  - Memory access patterns (pages, counters)")
        print(f"  - Call trace entries and statistics")
        print(f"  - CPU registers and stack")
        print(f"  - Disassembly at PC")
        print(f"\n{Colors.YELLOW}Press ENTER when ready to capture snapshot...{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}\n")
        
        try:
            input()
        except KeyboardInterrupt:
            print(f"\n{Colors.YELLOW}Interrupted by user{Colors.END}")
            
        # Stop all profilers and dump data
        self.stop_profilers()
        self.dump_session_data()
        
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.GREEN}{Colors.BOLD}✓ Debug session captured!{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}\n")
        
        return True


def main():
    parser = argparse.ArgumentParser(
        description='Capture comprehensive Z80 debug session data via WebAPI',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    
    parser.add_argument('--host', default='localhost',
                        help='WebAPI host (default: localhost)')
    parser.add_argument('--port', type=int, default=8090,
                        help='WebAPI port (default: 8090)')
    parser.add_argument('--uuid', default=None,
                        help='Specific emulator UUID')
    
    args = parser.parse_args()
    
    capture = DebugCapture(host=args.host, port=args.port)
    
    try:
        success = capture.run(emulator_uuid=args.uuid)
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print(f"\n\n{Colors.YELLOW}Capture interrupted{Colors.END}")
        sys.exit(130)
    except Exception as e:
        print(f"\n{Colors.RED}Unexpected error: {e}{Colors.END}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
