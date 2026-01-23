#!/usr/bin/env python3
"""
TR-DOS Analyzer Session Capture Harness

Simple script to activate TR-DOS analyzer, wait for user to manually operate
the emulator, then capture and dump all session data to JSON files.

Usage:
    python capture_trdos_session.py [--host HOST] [--port PORT] [--uuid UUID]
    
The script will:
1. Connect to emulator WebAPI
2. Load TR-DOS snapshot (if specified)
3. Load disk image (if specified)
4. Activate TR-DOS analyzer
5. Wait for user keypress (manual operation time)
6. Deactivate analyzer and dump session data to JSON files

Output files:
    - session_metadata.json    - Session info (snapshot, disk, timestamp)
    - semantic_events.json     - High-level TR-DOS events
    - raw_fdc_events.json      - Raw FDC port I/O with Z80 context
    - raw_breakpoint_events.json - Raw breakpoint hits with full Z80 state
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


class Colors:
    """ANSI color codes for terminal output"""
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    BOLD = '\033[1m'
    END = '\033[0m'


def find_project_root() -> Path:
    """Find the project root directory by searching upward for CMakeLists.txt."""
    current = Path(__file__).resolve().parent
    max_depth = 10
    for _ in range(max_depth):
        if (current / "CMakeLists.txt").exists():
            try:
                cmake_content = (current / "CMakeLists.txt").read_text()
                if "project(" in cmake_content or "PROJECT(" in cmake_content:
                    return current
            except:
                pass
        parent = current.parent
        if parent == current:
            break
        current = parent
    return Path(__file__).resolve().parent.parent.parent.parent


class TRDOSSessionCapture:
    """Manual capture harness for TR-DOS analyzer sessions"""
    
    def __init__(self, host: str = "localhost", port: int = 8090):
        self.base_url = f"http://{host}:{port}"
        self.emulator_uuid: Optional[str] = None
        self.session = requests.Session()
        self.session.headers.update({'Content-Type': 'application/json'})
        
        # Session metadata
        self.snapshot_path: Optional[str] = None
        self.disk_image_path: Optional[str] = None
        self.session_start: Optional[datetime] = None
        
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
        """Select an emulator instance"""
        self.print_header("Selecting emulator...")
        
        if uuid:
            self.emulator_uuid = uuid
            self.print_info(f"Using specified emulator: {uuid[:8]}...")
            return True
            
        emulators = self.discover_emulators()
        if not emulators:
            self.print_error("No running emulator instances found")
            self.print_info("Please start an emulator instance first")
            return False
            
        self.emulator_uuid = emulators[0]['id']
        self.print_success(f"Auto-selected emulator: {self.emulator_uuid[:8]}...")
        return True
        
    def load_snapshot(self, snapshot_path: Optional[str] = None) -> bool:
        """Load TR-DOS snapshot"""
        if not snapshot_path:
            # Try to find default TR-DOS snapshot
            project_root = find_project_root()
            default_snapshot = project_root / "testdata" / "loaders" / "z80" / "trdos-snapshot.z80"
            if default_snapshot.exists():
                snapshot_path = str(default_snapshot)
            else:
                self.print_info("No snapshot specified, skipping snapshot load")
                return True
                
        self.print_header(f"Loading snapshot: {Path(snapshot_path).name}")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/snapshot/load"
        payload = {"path": str(Path(snapshot_path).resolve())}
        
        try:
            response = self.session.post(url, json=payload, timeout=10)
            response.raise_for_status()
            result = response.json()
            if result.get('status') == 'success' or 'success' in result.get('message', '').lower():
                self.snapshot_path = str(Path(snapshot_path).resolve())
                self.print_success(f"Loaded: {Path(snapshot_path).name}")
                return True
            else:
                self.print_error(f"Failed to load snapshot: {result.get('message', 'Unknown error')}")
                return False
        except Exception as e:
            self.print_error(f"Failed to load snapshot: {e}")
            return False
            
    def load_disk(self, disk_path: Optional[str] = None) -> bool:
        """Load disk image into drive A"""
        if not disk_path:
            # Try to find a disk image
            project_root = find_project_root()
            testdata_dir = project_root / "testdata"
            for ext in ['*.trd', '*.scl']:
                matches = list(testdata_dir.rglob(ext))
                if matches:
                    disk_path = str(matches[0])
                    break
                    
        if not disk_path:
            self.print_info("No disk image specified, skipping disk load")
            return True
            
        self.print_header(f"Loading disk: {Path(disk_path).name}")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/disk/A/insert"
        payload = {"path": str(Path(disk_path).resolve())}
        
        try:
            response = self.session.post(url, json=payload, timeout=5)
            response.raise_for_status()
            self.disk_image_path = str(Path(disk_path).resolve())
            self.print_success(f"Loaded: {Path(disk_path).name}")
            return True
        except Exception as e:
            self.print_info(f"Could not load disk: {e}")
            return True  # Not critical
            
    def activate_analyzer(self) -> bool:
        """Activate TR-DOS analyzer and start session"""
        self.print_header("Activating TR-DOS analyzer...")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/analyzer/trdos/session"
        payload = {"action": "activate"}
        
        try:
            response = self.session.post(url, json=payload, timeout=5)
            response.raise_for_status()
            result = response.json()
            if result.get('success'):
                self.session_start = datetime.now()
                self.print_success("TR-DOS analyzer activated (session started)")
                return True
            else:
                self.print_error(f"Activation failed: {result.get('message', 'Unknown error')}")
                return False
        except Exception as e:
            self.print_error(f"Failed to activate analyzer: {e}")
            return False
            
    def get_semantic_events(self, limit: int = 10000) -> dict:
        """Retrieve semantic events"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/analyzer/trdos/events?limit={limit}"
        try:
            response = self.session.get(url, timeout=5)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            self.print_error(f"Failed to retrieve semantic events: {e}")
            return {}
            
    def get_raw_fdc_events(self, limit: int = 10000) -> dict:
        """Retrieve raw FDC events"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/analyzer/trdos/raw/fdc?limit={limit}"
        try:
            response = self.session.get(url, timeout=5)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            self.print_error(f"Failed to retrieve raw FDC events: {e}")
            return {}
            
    def get_raw_breakpoint_events(self, limit: int = 10000) -> dict:
        """Retrieve raw breakpoint events"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/analyzer/trdos/raw/breakpoints?limit={limit}"
        try:
            response = self.session.get(url, timeout=5)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            self.print_error(f"Failed to retrieve raw breakpoint events: {e}")
            return {}
            
    def deactivate_analyzer(self) -> bool:
        """Deactivate TR-DOS analyzer"""
        self.print_header("Deactivating analyzer...")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/analyzer/trdos/session"
        payload = {"action": "deactivate"}
        
        try:
            response = self.session.post(url, json=payload, timeout=5)
            response.raise_for_status()
            self.print_success("TR-DOS analyzer deactivated")
            return True
        except Exception as e:
            self.print_info(f"Failed to deactivate analyzer: {e}")
            return True  # Not critical
            
    def dump_session_data(self):
        """Dump all session data to JSON files"""
        self.print_header("Dumping session data to files...")
        
        # Session metadata
        metadata = {
            "session_start": self.session_start.isoformat() if self.session_start else None,
            "session_end": datetime.now().isoformat(),
            "emulator_id": self.emulator_uuid,
            "snapshot": {
                "path": self.snapshot_path,
                "filename": Path(self.snapshot_path).name if self.snapshot_path else None,
            },
            "disk_image": {
                "path": self.disk_image_path,
                "filename": Path(self.disk_image_path).name if self.disk_image_path else None,
            },
            "analyzer": "trdos"
        }
        
        with open('session_metadata.json', 'w') as f:
            json.dump(metadata, f, indent=2)
        self.print_success("Saved: session_metadata.json")
        
        # Semantic events
        semantic_data = self.get_semantic_events()
        if semantic_data:
            with open('semantic_events.json', 'w') as f:
                json.dump(semantic_data, f, indent=2)
            event_count = len(semantic_data.get('events', []))
            self.print_success(f"Saved: semantic_events.json ({event_count} events)")
            
        # Raw FDC events
        raw_fdc_data = self.get_raw_fdc_events()
        if raw_fdc_data:
            with open('raw_fdc_events.json', 'w') as f:
                json.dump(raw_fdc_data, f, indent=2)
            event_count = len(raw_fdc_data.get('events', []))
            self.print_success(f"Saved: raw_fdc_events.json ({event_count} events)")
            
        # Raw breakpoint events
        raw_bp_data = self.get_raw_breakpoint_events()
        if raw_bp_data:
            with open('raw_breakpoint_events.json', 'w') as f:
                json.dump(raw_bp_data, f, indent=2)
            event_count = len(raw_bp_data.get('events', []))
            self.print_success(f"Saved: raw_breakpoint_events.json ({event_count} events)")
            
    def run(self, emulator_uuid: Optional[str] = None, 
            snapshot_path: Optional[str] = None,
            disk_path: Optional[str] = None) -> bool:
        """Run the capture session"""
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.BOLD}TR-DOS Analyzer Session Capture{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}")
        
        # Connect and setup
        if not self.check_connection():
            return False
            
        if not self.select_emulator(emulator_uuid):
            return False
            
        # Note: User should load snapshot/disk manually before running this script
        # The script only activates the analyzer for whatever is currently loaded
            
        # Activate analyzer
        if not self.activate_analyzer():
            return False
            
        # Wait for user
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.GREEN}Analyzer is now active!{Colors.END}")
        print(f"{Colors.BOLD}Perform your TR-DOS operations in the emulator.{Colors.END}")
        print(f"{Colors.YELLOW}Press ENTER when done to capture and save session data...{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}\n")
        
        try:
            input()
        except KeyboardInterrupt:
            print(f"\n{Colors.YELLOW}Interrupted by user{Colors.END}")
            
        # Deactivate first, then dump data (testing data persistence after deactivation)
        self.deactivate_analyzer()
        self.dump_session_data()
        
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.GREEN}{Colors.BOLD}✓ Session data captured successfully!{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}\n")
        
        return True


def main():
    parser = argparse.ArgumentParser(
        description='Capture TR-DOS analyzer session data manually',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Auto-discover emulator, use default snapshot/disk
  python capture_trdos_session.py
  
  # Specify custom paths
  python capture_trdos_session.py --snapshot /path/to/snapshot.z80 --disk /path/to/disk.trd
  
  # Specify emulator UUID
  python capture_trdos_session.py --uuid 12345678-1234-1234-1234-123456789abc
        """
    )
    
    parser.add_argument('--host', default='localhost',
                        help='WebAPI host (default: localhost)')
    parser.add_argument('--port', type=int, default=8090,
                        help='WebAPI port (default: 8090)')
    parser.add_argument('--uuid', default=None,
                        help='Specific emulator UUID (auto-discovers if not provided)')
    parser.add_argument('--snapshot', default=None,
                        help='TR-DOS snapshot path (auto-finds if not provided)')
    parser.add_argument('--disk', default=None,
                        help='Disk image path (auto-finds if not provided)')
    
    args = parser.parse_args()
    
    capture = TRDOSSessionCapture(host=args.host, port=args.port)
    
    try:
        success = capture.run(
            emulator_uuid=args.uuid,
            snapshot_path=args.snapshot,
            disk_path=args.disk
        )
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
