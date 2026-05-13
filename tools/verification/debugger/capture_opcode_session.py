#!/usr/bin/env python3
"""
Opcode Profiler Session Capture Script

Connects to emulator WebAPI, activates opcode profiler, waits for user to 
capture Z80 execution, then dumps profiler data to local results folder.

Usage:
    python capture_opcode_session.py [--host HOST] [--port PORT] [--name SESSION_NAME]
    
The script will:
1. Connect to emulator WebAPI
2. Enable opcode profiler feature and start capture session
3. Wait for user keypress (manual operation time)
4. Stop profiler and retrieve counters + trace
5. Save all data to timestamped results folder

Output files in results/YYYY-MM-DD-HH-MM-opcodeprofiler/:
    - session_metadata.yaml    - Session info (timestamp, duration)
    - profiler_status.yaml     - Final profiler status
    - opcode_counters.yaml     - Opcode execution counts
    - opcode_trace.yaml        - Recent execution trace
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


class OpcodeProfilerCapture:
    """Capture harness for Z80 opcode profiler sessions via WebAPI"""
    
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
            self.print_info("Make sure the emulator is running with WebAPI enabled")
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
        """Select an emulator instance, creating one if none exists"""
        self.print_header("Selecting emulator...")
        
        if uuid:
            self.emulator_uuid = uuid
            self.print_info(f"Using specified emulator: {uuid[:8]}...")
            return True
            
        emulators = self.discover_emulators()
        if not emulators:
            self.print_info("No running emulator instances found, creating one...")
            if not self._create_emulator():
                return False
            # Re-discover after creation
            emulators = self.discover_emulators()
            if not emulators:
                self.print_error("Failed to find created emulator")
                return False
            
        self.emulator_uuid = emulators[0]['id']
        self.print_success(f"Auto-selected emulator: {self.emulator_uuid[:8]}...")
        return True
        
    def _create_emulator(self, model: str = "PENTAGON") -> bool:
        """Create and start a new emulator instance"""
        try:
            url = f"{self.base_url}/api/v1/emulator/start"
            response = self.session.post(url, json={"model": model}, timeout=10)
            response.raise_for_status()
            result = response.json()
            self.print_success(f"Created and started emulator: {result.get('id', 'unknown')[:8]}... (model: {model})")
            return True
        except Exception as e:
            self.print_error(f"Failed to create emulator: {e}")
            return False
        
    def start_profiler(self) -> bool:
        """Start all profiler capture sessions (opcode, memory, calltrace)"""
        self.print_header("Starting all profiler sessions...")
        
        # Use unified endpoint to start all profilers at once
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/profiler/start"
        
        try:
            response = self.session.post(url, timeout=5)
            response.raise_for_status()
            result = response.json()
            
            self.session_start = datetime.now()
            self.print_success("All profiler sessions started (opcode, memory, calltrace)")
            
            # Show individual profiler status
            status = result.get('status', {})
            for profiler, state in status.items():
                self.print_info(f"  {profiler}: {state}")
            
            return True
        except Exception as e:
            self.print_error(f"Failed to start profilers: {e}")
            return False
            
    def stop_profiler(self) -> bool:
        """Stop all profiler capture sessions"""
        self.print_header("Stopping all profiler sessions...")
        
        # Use unified endpoint to stop all profilers
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/profiler/stop"
        
        try:
            response = self.session.post(url, timeout=5)
            response.raise_for_status()
            self.print_success("All profiler sessions stopped")
            return True
        except Exception as e:
            self.print_info(f"Failed to stop profilers: {e}")
            return True  # Not critical
            
    def get_profiler_status(self) -> dict:
        """Get unified profiler status"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/profiler/status"
        try:
            response = self.session.get(url, timeout=5)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            self.print_error(f"Failed to retrieve profiler status: {e}")
            return {}
            
    def get_opcode_counters(self, limit: int = 100) -> dict:
        """Get opcode execution counters"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/profiler/opcode/counters?limit={limit}"
        try:
            response = self.session.get(url, timeout=10)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            self.print_error(f"Failed to retrieve opcode counters: {e}")
            return {}
            
    def get_opcode_trace(self, count: int = 500) -> dict:
        """Get recent execution trace"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/profiler/opcode/trace?count={count}"
        try:
            response = self.session.get(url, timeout=10)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            self.print_error(f"Failed to retrieve execution trace: {e}")
            return {}
            
    def create_results_dir(self) -> Path:
        """Create timestamped results directory"""
        timestamp = datetime.now().strftime("%Y-%m-%d-%H-%M-profiler")
        results_dir = Path(__file__).parent / "results" / timestamp
        results_dir.mkdir(parents=True, exist_ok=True)
        return results_dir
        
    def dump_session_data(self):
        """Dump all session data to files"""
        self.print_header("Dumping session data to files...")
        
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
            "capture_type": "all_profilers"
        }
        
        metadata_path = self.results_dir / "session_metadata.yaml"
        with open(metadata_path, 'w') as f:
            yaml.dump(metadata, f, default_flow_style=False, sort_keys=False)
        self.print_success(f"Saved: {metadata_path.name}")
        
        # Profiler status
        status = self.get_profiler_status()
        if status:
            status_path = self.results_dir / "profiler_status.yaml"
            with open(status_path, 'w') as f:
                yaml.dump(status, f, default_flow_style=False, sort_keys=False)
            self.print_success(f"Saved: {status_path.name}")
        
        # Opcode counters (top 100)
        counters = self.get_opcode_counters(100)
        if counters:
            counters_path = self.results_dir / "opcode_counters.yaml"
            with open(counters_path, 'w') as f:
                yaml.dump(counters, f, default_flow_style=False, sort_keys=False)
            count = counters.get('count', 0)
            total = counters.get('total_executions', 0)
            self.print_success(f"Saved: {counters_path.name} ({count} opcodes, {total:,} executions)")
        
        # Execution trace (last 500 entries)
        trace = self.get_opcode_trace(500)
        if trace:
            trace_path = self.results_dir / "opcode_trace.yaml"
            with open(trace_path, 'w') as f:
                yaml.dump(trace, f, default_flow_style=False, sort_keys=False)
            count = trace.get('returned_count', 0)
            self.print_success(f"Saved: {trace_path.name} ({count} trace entries)")
        
        # Memory profiler status (includes access counts)
        memory_status = self.get_memory_status()
        if memory_status:
            memory_path = self.results_dir / "memory_status.yaml"
            with open(memory_path, 'w') as f:
                yaml.dump(memory_status, f, default_flow_style=False, sort_keys=False)
            self.print_success(f"Saved: {memory_path.name}")
        
        # Call trace profiler data - entries
        entries = self.get_calltrace_entries(500)
        if entries and entries.get('entries'):
            entries_path = self.results_dir / "calltrace_entries.yaml"
            with open(entries_path, 'w') as f:
                yaml.dump(entries, f, default_flow_style=False, sort_keys=False)
            count = len(entries.get('entries', []))
            self.print_success(f"Saved: {entries_path.name} ({count} entries)")
        
        # Call trace profiler status
        calltrace_status = self.get_calltrace_status()
        if calltrace_status:
            status_path = self.results_dir / "calltrace_status.yaml"
            with open(status_path, 'w') as f:
                yaml.dump(calltrace_status, f, default_flow_style=False, sort_keys=False)
            self.print_success(f"Saved: {status_path.name}")
        
        self.print_info(f"Results saved to: {self.results_dir}")
    
    def get_memory_status(self) -> dict:
        """Get memory profiler status"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/profiler/memory/status"
        try:
            response = self.session.get(url, timeout=10)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            self.print_error(f"Failed to retrieve memory status: {e}")
            return {}
    
    def get_calltrace_entries(self, count: int = 500) -> dict:
        """Get call trace entries"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/profiler/calltrace/entries?count={count}"
        try:
            response = self.session.get(url, timeout=10)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            self.print_error(f"Failed to retrieve calltrace entries: {e}")
            return {}
    
    def get_calltrace_status(self) -> dict:
        """Get call trace profiler status"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/profiler/calltrace/status"
        try:
            response = self.session.get(url, timeout=10)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            self.print_error(f"Failed to retrieve calltrace status: {e}")
            return {}
        
    def run(self, emulator_uuid: Optional[str] = None) -> bool:
        """Run the capture session"""
        
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.BOLD}Z80 Unified Profiler Session Capture{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}")
        
        # Connect and setup
        if not self.check_connection():
            return False
            
        if not self.select_emulator(emulator_uuid):
            return False
            
        # Start profiler (auto-enables feature)
        if not self.start_profiler():
            return False
            
        # Wait for user
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.GREEN}All profilers are now capturing!{Colors.END}")
        print(f"{Colors.BOLD}Run your Z80 code in the emulator.{Colors.END}")
        print(f"{Colors.CYAN}Active profilers:{Colors.END}")
        print(f"  - Opcode: execution counts + trace")
        print(f"  - Memory: read/write/execute patterns")
        print(f"  - Calltrace: CALL/RET/JP/JR events")
        print(f"\n{Colors.YELLOW}Press ENTER when done to capture and save session data...{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}\n")
        
        try:
            input()
        except KeyboardInterrupt:
            print(f"\n{Colors.YELLOW}Interrupted by user{Colors.END}")
            
        # Stop and dump
        self.stop_profiler()
        self.dump_session_data()
        
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.GREEN}{Colors.BOLD}✓ Session data captured successfully!{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}\n")
        
        return True


def main():
    parser = argparse.ArgumentParser(
        description='Capture Z80 opcode profiler session data via WebAPI',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Connect with default settings (localhost:8090)
  # Output: results/2026-01-27-19-00-opcodeprofiler/
  python capture_opcode_session.py
  
  # Specify custom host/port
  python capture_opcode_session.py --host 192.168.1.10 --port 8090
  
  # Specify emulator UUID (if multiple running)
  python capture_opcode_session.py --uuid 12345678-1234-1234-1234-123456789abc
        """
    )
    
    parser.add_argument('--host', default='localhost',
                        help='WebAPI host (default: localhost)')
    parser.add_argument('--port', type=int, default=8090,
                        help='WebAPI port (default: 8090)')
    parser.add_argument('--uuid', default=None,
                        help='Specific emulator UUID (auto-discovers if not provided)')
    
    args = parser.parse_args()
    
    capture = OpcodeProfilerCapture(host=args.host, port=args.port)
    
    try:
        success = capture.run(emulator_uuid=args.uuid)
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print(f"\n\n{Colors.YELLOW}Capture interrupted by user{Colors.END}")
        sys.exit(130)
    except Exception as e:
        print(f"\n{Colors.RED}Unexpected error: {e}{Colors.END}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
