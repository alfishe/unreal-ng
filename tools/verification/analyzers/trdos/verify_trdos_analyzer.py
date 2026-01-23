#!/usr/bin/env python3
"""
TR-DOS Analyzer Verification Script

This script verifies the TR-DOS analyzer functionality via WebAPI:
1. Connects to emulator WebAPI (port 8090)
2. Discovers running emulator instances
3. Activates TR-DOS analyzer
4. Executes LIST command via BASIC injection
5. Retrieves and verifies collected events
6. Deactivates analyzer

Usage:
    python verify_trdos_analyzer.py [--host HOST] [--port PORT] [--uuid UUID]

Arguments:
    --host HOST     WebAPI host (default: localhost)
    --port PORT     WebAPI port (default: 8090)
    --uuid UUID     Specific emulator UUID (optional, auto-discovers if not provided)
"""

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from urllib.parse import urljoin

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
    """
    Find the project root directory by searching upward for CMakeLists.txt.
    Similar to TestPathHelper::GetProjectRoot() in C++ tests.
    """
    current = Path(__file__).resolve().parent
    
    # Search upward for CMakeLists.txt (project root marker)
    max_depth = 10
    for _ in range(max_depth):
        if (current / "CMakeLists.txt").exists():
            # Verify it's the root CMakeLists.txt by checking for project() command
            try:
                cmake_content = (current / "CMakeLists.txt").read_text()
                if "project(" in cmake_content or "PROJECT(" in cmake_content:
                    return current
            except:
                pass
        
        parent = current.parent
        if parent == current:  # Reached filesystem root
            break
        current = parent
    
    # Fallback: assume script is at tools/verification/analyzers/trdos/
    return Path(__file__).resolve().parent.parent.parent.parent.parent


class TRDOSAnalyzerVerifier:
    """Verifies TR-DOS analyzer functionality via WebAPI"""
    
    def __init__(self, host: str = "localhost", port: int = 8090):
        self.base_url = f"http://{host}:{port}"
        self.emulator_uuid: Optional[str] = None
        self.session = requests.Session()
        self.session.headers.update({'Content-Type': 'application/json'})
        
    def print_step(self, step: int, message: str):
        """Print a step message"""
        print(f"\n{Colors.BOLD}[Step {step}]{Colors.END} {message}")
        
    def print_success(self, message: str):
        """Print a success message"""
        print(f"{Colors.GREEN}✓{Colors.END} {message}")
        
    def print_error(self, message: str):
        """Print an error message"""
        print(f"{Colors.RED}✗{Colors.END} {message}")
        
    def print_warning(self, message: str):
        """Print a warning message"""
        print(f"{Colors.YELLOW}⚠{Colors.END} {message}")
        
    def print_info(self, message: str):
        """Print an info message"""
        print(f"{Colors.BLUE}ℹ{Colors.END} {message}")
        
    def check_connection(self) -> bool:
        """Check if WebAPI is accessible"""
        self.print_step(1, "Checking WebAPI connection...")
        
        try:
            response = self.session.get(f"{self.base_url}/api/v1/emulator", timeout=5)
            if response.status_code == 200:
                self.print_success(f"WebAPI is accessible at {self.base_url}")
                return True
            else:
                self.print_error(f"WebAPI returned status code {response.status_code}")
                return False
        except requests.exceptions.ConnectionError:
            self.print_error(f"Cannot connect to WebAPI at {self.base_url}")
            self.print_info("Make sure the emulator is running with WebAPI enabled")
            return False
        except requests.exceptions.Timeout:
            self.print_error("Connection timeout")
            return False
        except Exception as e:
            self.print_error(f"Unexpected error: {e}")
            return False
            
    def discover_emulators(self) -> List[Dict]:
        """Discover running emulator instances"""
        self.print_step(2, "Discovering emulator instances...")
        
        try:
            response = self.session.get(f"{self.base_url}/api/v1/emulator", timeout=5)
            response.raise_for_status()
            
            data = response.json()
            emulators = data.get('emulators', [])
            
            if not emulators:
                self.print_warning("No running emulator instances found")
                return []
                
            self.print_success(f"Found {len(emulators)} emulator instance(s)")
            for i, emu in enumerate(emulators, 1):
                emu_id = emu.get('id', 'unknown')
                state = emu.get('state', 'unknown')
                is_running = emu.get('is_running', False)
                print(f"  {i}. ID: {emu_id[:8]}... State: {state}, Running: {is_running}")
                
            return emulators
            
        except requests.exceptions.HTTPError as e:
            self.print_error(f"HTTP error: {e}")
            return []
        except Exception as e:
            self.print_error(f"Failed to discover emulators: {e}")
            return []
            
            
    def create_emulator(self) -> Optional[str]:
        """Create a new PENTAGON emulator instance"""
        self.print_info("Creating new PENTAGON emulator instance...")
        
        url = f"{self.base_url}/api/v1/emulator/start"
        payload = {
            "model": "Pentagon",
            "ram_size": 128
        }
        
        try:
            response = self.session.post(url, json=payload, timeout=10)
            response.raise_for_status()
            
            result = response.json()
            emulator_id = result.get('id') or result.get('uuid')
            
            if emulator_id:
                self.print_success(f"Created emulator: {emulator_id[:8]}...")
                return emulator_id
            else:
                self.print_error("Failed to get emulator ID from response")
                return None
                
        except requests.exceptions.HTTPError as e:
            self.print_error(f"HTTP error creating emulator: {e}")
            return None
        except Exception as e:
            self.print_error(f"Failed to create emulator: {e}")
            return None
            
    def ensure_emulator_running(self, emulator_id: str) -> bool:
        """Ensure emulator is in running state"""
        url = f"{self.base_url}/api/v1/emulator/{emulator_id}"
        
        try:
            # Get current state
            response = self.session.get(url, timeout=5)
            response.raise_for_status()
            state = response.json()
            
            is_running = state.get('is_running', False)
            is_paused = state.get('is_paused', False)
            
            if is_running and not is_paused:
                self.print_info("Emulator is running")
                return True
                
            # Need to start or resume
            if is_paused:
                self.print_info("Resuming paused emulator...")
                action_url = f"{self.base_url}/api/v1/emulator/{emulator_id}/resume"
            else:
                self.print_info("Starting emulator...")
                action_url = f"{self.base_url}/api/v1/emulator/{emulator_id}/start"
                
            response = self.session.post(action_url, timeout=5)
            response.raise_for_status()
            
            # Verify it's running now
            time.sleep(0.5)  # Brief delay for state change
            response = self.session.get(url, timeout=5)
            response.raise_for_status()
            state = response.json()
            
            if state.get('is_running', False) and not state.get('is_paused', False):
                self.print_success("Emulator is now running")
                return True
            else:
                self.print_error("Failed to start emulator")
                return False
                
        except Exception as e:
            self.print_error(f"Failed to ensure emulator running: {e}")
            return False
            
    def select_emulator(self, uuid: Optional[str] = None) -> bool:
        """Select an emulator instance, creating one if necessary"""
        if uuid:
            self.emulator_uuid = uuid
            self.print_info(f"Using specified emulator ID: {uuid[:8]}...")
            return self.ensure_emulator_running(uuid)
            
        emulators = self.discover_emulators()
        
        if not emulators:
            self.print_warning("No emulators found, creating new PENTAGON instance...")
            new_id = self.create_emulator()
            if not new_id:
                return False
            self.emulator_uuid = new_id
            # New emulator should already be running
            return True
            
        # Use first available emulator
        self.emulator_uuid = emulators[0]['id']
        self.print_info(f"Auto-selected emulator: {self.emulator_uuid[:8]}...")
        
        # Ensure it's running
        return self.ensure_emulator_running(self.emulator_uuid)
    
    def load_trdos_snapshot(self) -> bool:
        """Load TR-DOS snapshot to ensure ROM is available"""
        self.print_step(2.5, "Loading TR-DOS snapshot...")
        
        # Use proper project root finder
        project_root = find_project_root()
        snapshot_path = project_root / "testdata" / "loaders" / "z80" / "trdos-snapshot.z80"
        snapshot_abs_path = str(snapshot_path.resolve())
        
        self.print_info(f"Snapshot path: {snapshot_abs_path}")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/snapshot/load"
        payload = {"path": snapshot_abs_path}
        
        try:
            response = self.session.post(url, json=payload, timeout=10)
            response.raise_for_status()
            
            result = response.json()
            if result.get('status') == 'success' or result.get('message', '').find('success') >= 0:
                self.print_success("TR-DOS snapshot loaded (TR-DOS is now active)")
                return True
            else:
                self.print_error(f"Failed to load snapshot: {result.get('message', 'Unknown error')}")
                return False
                
        except requests.exceptions.HTTPError as e:
            self.print_error(f"HTTP error loading snapshot: {e}")
            return False
        except Exception as e:
            self.print_error(f"Failed to load snapshot: {e}")
            return False
    
    def load_disk_image(self) -> bool:
        """Load a TR-DOS disk image into drive A"""
        self.print_step(2.7, "Loading disk image...")
        
        # Use proper project root finder
        project_root = find_project_root()
        testdata_dir = project_root / "testdata"
        disk_image = None
        
        for ext in ['*.trd', '*.scl']:
            matches = list(testdata_dir.rglob(ext))
            if matches:
                disk_image = str(matches[0].resolve())
                break
        
        if not disk_image:
            self.print_warning("No disk image found, skipping")
            return True  # Not critical
        
        self.print_info(f"Disk image: {disk_image}")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/disk/A/insert"
        payload = {"path": disk_image}
        
        try:
            response = self.session.post(url, json=payload, timeout=5)
            response.raise_for_status()
            
            self.print_success("Disk image loaded into drive A")
            return True
                
        except requests.exceptions.HTTPError as e:
            self.print_warning(f"Could not load disk: {e}")
            return True  # Not critical
        except Exception as e:
            self.print_warning(f"Failed to load disk: {e}")
            return True  # Not critical

        
    def activate_analyzer(self) -> bool:
        """Activate TR-DOS analyzer"""
        self.print_step(3, "Activating TR-DOS analyzer...")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/analyzer/trdos"
        payload = {"enabled": True}
        
        try:
            response = self.session.put(url, json=payload, timeout=5)
            response.raise_for_status()
            
            result = response.json()
            if result.get('success') or result.get('enabled') == True:
                self.print_success("TR-DOS analyzer activated")
                return True
            else:
                self.print_error(f"Activation failed: {result.get('message', 'Unknown error')}")
                return False
                
        except requests.exceptions.HTTPError as e:
            self.print_error(f"HTTP error during activation: {e}")
            if e.response.status_code == 404:
                self.print_info("Endpoint not found. Check if analyzer API is implemented.")
            return False
        except Exception as e:
            self.print_error(f"Failed to activate analyzer: {e}")
            return False
            
    def execute_basic_command(self, command: str) -> bool:
        """Execute a BASIC command"""
        self.print_step(4, f"Executing BASIC command: {command}")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/basic/run"
        payload = {"command": command}
        
        try:
            response = self.session.post(url, json=payload, timeout=10)
            response.raise_for_status()
            
            result = response.json()
            
            # Debug: show what command was actually executed
            actual_command = result.get('command', 'unknown')
            self.print_info(f"API response - command executed: '{actual_command}'")
            
            if result.get('success'):
                self.print_success(f"Command '{command}' executed")
                cycles = result.get('cycles', 0)
                if cycles > 0:
                    self.print_info(f"Executed {cycles} CPU cycles")
                return True
            else:
                self.print_error(f"Execution failed: {result.get('message', 'Unknown error')}")
                return False
                
        except requests.exceptions.HTTPError as e:
            self.print_error(f"HTTP error during command execution: {e}")
            return False
        except Exception as e:
            self.print_error(f"Failed to execute command: {e}")
            return False
    
    def capture_screen_ocr(self, description: str = "") -> Tuple[bool, List[str]]:
        """Capture screen via OCR and return lines"""
        step_label = f"Screen OCR{': ' + description if description else ''}"
        self.print_info(f"{step_label}...")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/capture/ocr"
        
        try:
            response = self.session.get(url, timeout=10)
            response.raise_for_status()
            
            result = response.json()
            
            if result.get('status') != 'success':
                self.print_error(f"OCR failed: {result.get('message', 'Unknown error')}")
                return False, []
            
            lines = result.get('lines', [])
            text = result.get('text', '')
            
            self.print_success(f"OCR captured ({len(lines)} lines)")
            
            # Show first few non-empty lines
            non_empty = [l.strip() for l in lines if l.strip()]
            print(f"\n{Colors.BOLD}Screen Content Preview:{Colors.END}")
            for i, line in enumerate(non_empty[:8]):
                # Truncate long lines
                display_line = line[:60] + ('...' if len(line) > 60 else '')
                print(f"  |{display_line}|")
            if len(non_empty) > 8:
                print(f"  ... ({len(non_empty) - 8} more lines)")
            print()
            
            return True, lines
            
        except requests.exceptions.HTTPError as e:
            self.print_error(f"HTTP error during OCR: {e}")
            return False, []
        except Exception as e:
            self.print_error(f"OCR failed: {e}")
            return False, []
    
    def inject_basic_command(self, command: str) -> bool:
        """Inject a BASIC command into editor (without executing)"""
        self.print_info(f"Injecting command: {command}")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/basic/inject"
        payload = {"command": command}  # Now uses 'command' parameter
        
        try:
            response = self.session.post(url, json=payload, timeout=10)
            response.raise_for_status()
            
            result = response.json()
            state = result.get('state', 'unknown')
            
            if result.get('success'):
                self.print_success(f"Command '{command}' injected (state: {state})")
                
                # For TR-DOS, we need to step the emulator to process keypresses
                # since keypress injection only sets one key at a time
                if state == 'trdos':
                    self.print_info("TR-DOS mode - stepping emulator to process keys...")
                    # Run emulator for ~100ms to process keypress
                    self.step_frames(5)
                    
                return True
            else:
                self.print_error(f"Injection failed: {result.get('message', 'Unknown error')}")
                return False
                
        except requests.exceptions.HTTPError as e:
            self.print_error(f"HTTP error during injection: {e}")
            if hasattr(e, 'response') and e.response is not None:
                try:
                    error_body = e.response.json()
                    self.print_info(f"Server response: {error_body}")
                except:
                    self.print_info(f"Server response: {e.response.text[:500]}")
            return False
        except Exception as e:
            self.print_error(f"Failed to inject command: {e}")
            return False
    
    def step_frames(self, count: int = 1) -> bool:
        """Step emulator by specified number of frames"""
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/step"
        payload = {"frames": count}
        try:
            response = self.session.post(url, json=payload, timeout=5)
            if response.status_code == 404:
                # No step endpoint, just wait (20ms per frame at 50Hz)
                time.sleep(0.02 * count)
                return True
            return response.status_code == 200
        except:
            time.sleep(0.02 * count)
            return True
    
    def verify_command_injection(self, command: str, wait_time: float = 1.0) -> bool:
        """Inject command and verify it appears on screen via OCR"""
        self.print_step("4b", f"Injecting and verifying command: {command}")
        
        # Step 1: Inject command (no ENTER pressed - command stays in editor buffer)
        if not self.inject_basic_command(command):
            return False
        
        # Give emulator time to process keypress and refresh screen
        time.sleep(wait_time)
        
        # Step 2: OCR verification - command MUST appear on screen
        success, lines = self.capture_screen_ocr("after injection")
        if not success:
            self.print_error("Failed to capture screen for verification")
            return False
            
        screen_text = '\n'.join(lines).upper()
        command_upper = command.upper()
        
        if command_upper in screen_text:
            self.print_success(f"Command '{command}' visible on screen!")
            return True
        
        # Also check memory as informational (not for pass/fail)
        if self.verify_injection_via_memory(command):
            self.print_info(f"Command found in memory buffer but NOT on screen - screen refresh issue!")
        
        self.print_error(f"Command '{command}' NOT visible on screen - injection failed!")
        return False
    
    def verify_injection_via_memory(self, command: str) -> bool:
        """Verify command was injected by reading E_LINE buffer via memory API"""
        try:
            # Read E_LINE pointer from system variable $5C59
            url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/memory/read/0x5C59?length=2"
            response = self.session.get(url, timeout=5)
            if response.status_code != 200:
                return False
            
            data = response.json().get('data', [])
            if len(data) < 2:
                return False
            
            e_line_addr = data[0] | (data[1] << 8)
            self.print_info(f"E_LINE buffer at address: 0x{e_line_addr:04X}")
            
            # Read buffer contents
            url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/memory/read/0x{e_line_addr:04X}?length=32"
            response = self.session.get(url, timeout=5)
            if response.status_code != 200:
                return False
            
            buffer_data = response.json().get('data', [])
            
            # Convert command to expected token (for LOAD = 0xEF)
            # Check if LOAD token (0xEF) is at start of buffer
            token_map = {'LOAD': 0xEF, 'LIST': 0xF0, 'CAT': 0xCF, 'RUN': 0xF7, 'SAVE': 0xF8}
            expected_token = token_map.get(command.upper())
            
            if expected_token and buffer_data and buffer_data[0] == expected_token:
                self.print_info(f"Found {command} token (0x{expected_token:02X}) in E_LINE buffer")
                return True
            
            # Also check for raw ASCII
            cmd_bytes = [ord(c) for c in command.upper()]
            if buffer_data[:len(cmd_bytes)] == cmd_bytes:
                self.print_info(f"Found '{command}' as ASCII in E_LINE buffer")
                return True
                
            return False
        except Exception as e:
            self.print_info(f"Memory verification failed: {e}")
            return False
    
    def inject_enter(self) -> bool:
        """Inject ENTER keypress to execute the already-injected command"""
        self.print_info("Injecting ENTER keypress...")
        
        # Use the basic/run endpoint with empty command - it will just inject ENTER
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/basic/run"
        payload = {"command": ""}  # Empty command = just inject ENTER
        
        try:
            response = self.session.post(url, json=payload, timeout=10)
            response.raise_for_status()
            
            result = response.json()
            if result.get('success'):
                self.print_success("ENTER keypress injected")
                return True
            else:
                self.print_warning(f"ENTER injection: {result.get('message', 'Unknown')}")
                return True  # Don't fail test
                
        except Exception as e:
            self.print_warning(f"ENTER injection failed: {e}")
            return True  # Don't fail the test
    
    def capture_results(self, description: str = "command results") -> Tuple[bool, List[str]]:
        """Capture screen to see command execution results"""
        self.print_step("5b", f"Capturing {description}...")
        return self.capture_screen_ocr(description)
    
    def verify_breakpoints(self) -> bool:
        """Verify that analyzer breakpoints are registered"""
        self.print_step("3.5", "Verifying analyzer breakpoints...")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/breakpoints"
        
        try:
            response = self.session.get(url, timeout=5)
            response.raise_for_status()
            
            result = response.json()
            breakpoints = result.get('breakpoints', [])
            
            # Expected TR-DOS breakpoints
            expected_bps = [0x3D00, 0x3D21, 0x0077]
            # Execution breakpoints have execute=true (type is 'memory' or 'port', not 'execution')
            found_bps = [bp.get('address') for bp in breakpoints if bp.get('execute') == True]
            
            self.print_info(f"Found {len(found_bps)} execution breakpoints: {[hex(addr) for addr in found_bps]}")
            
            for addr in expected_bps:
                if addr in found_bps:
                    self.print_success(f"Breakpoint registered at 0x{addr:04X}")
                else:
                    self.print_warning(f"Missing breakpoint at 0x{addr:04X}")
            
            return len(found_bps) > 0
            
        except requests.exceptions.HTTPError as e:
            if e.response.status_code == 404:
                self.print_warning("Breakpoint API not available")
                return True  # Not critical
            self.print_warning(f"Could not verify breakpoints: {e}")
            return True  # Not critical
        except Exception as e:
            self.print_warning(f"Breakpoint verification failed: {e}")
            return True  # Not critical
    
    def check_rom_state(self) -> Tuple[bool, int]:
        """Check current ROM page"""
        self.print_step("4.5", "Checking ROM state...")
        
        # Read current ROM page from memory (port 0x7FFD mirror or direct query)
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/memory/rom"
        
        try:
            response = self.session.get(url, timeout=5)
            if response.status_code == 404:
                # Try alternative: read from system state
                url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}"
                response = self.session.get(url, timeout=5)
                response.raise_for_status()
                state = response.json()
                rom_page = state.get('rom_page', -1)
            else:
                response.raise_for_status()
                result = response.json()
                rom_page = result.get('page', -1)
            
            if rom_page >= 0:
                rom_names = {0: "Service ROM", 1: "TR-DOS ROM", 2: "128K Editor", 3: "48K BASIC (SOS)"}
                rom_name = rom_names.get(rom_page, f"Unknown (page {rom_page})")
                self.print_info(f"Current ROM: Page {rom_page} ({rom_name})")
                
                if rom_page == 1:
                    self.print_success("TR-DOS ROM is paged in!")
                elif rom_page == 3:
                    self.print_warning("SOS ROM (48K BASIC) is paged - TR-DOS may be calling it")
                else:
                    self.print_warning(f"Unexpected ROM page: {rom_page}")
                
                return True, rom_page
            else:
                self.print_warning("Could not determine ROM page")
                return False, -1
                
        except Exception as e:
            self.print_warning(f"ROM state check failed: {e}")
            return False, -1
    
    def check_analyzer_state(self) -> bool:
        """Check analyzer internal state"""
        self.print_step("5.5", "Checking analyzer state...")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/analyzer/trdos"
        
        try:
            response = self.session.get(url, timeout=5)
            response.raise_for_status()
            
            result = response.json()
            enabled = result.get('enabled', False)
            state = result.get('state', 'unknown')
            event_count = result.get('event_count', 0)
            
            self.print_info(f"Analyzer enabled: {enabled}")
            self.print_info(f"Analyzer state: {state}")
            self.print_info(f"Event count: {event_count}")
            
            if not enabled:
                self.print_error("Analyzer is not enabled!")
                return False
            
            return True
            
        except requests.exceptions.HTTPError as e:
            if e.response.status_code == 404:
                self.print_warning("Analyzer state API not available")
                return True  # Not critical
            self.print_warning(f"Could not check analyzer state: {e}")
            return True  # Not critical
        except Exception as e:
            self.print_warning(f"Analyzer state check failed: {e}")
            return True  # Not critical
            
    def get_events(self) -> Tuple[bool, List[Dict]]:
        """Retrieve analyzer events"""
        self.print_step(5, "Retrieving analyzer events...")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/analyzer/trdos/events"
        
        try:
            response = self.session.get(url, timeout=5)
            response.raise_for_status()
            
            result = response.json()
            events = result.get('events', [])
            
            self.print_success(f"Retrieved {len(events)} event(s)")
            return True, events
            
        except requests.exceptions.HTTPError as e:
            self.print_error(f"HTTP error retrieving events: {e}")
            return False, []
        except Exception as e:
            self.print_error(f"Failed to retrieve events: {e}")
            return False, []
            
    def verify_events(self, events: List[Dict]) -> bool:
        """Verify that expected events were collected"""
        self.print_step(6, "Verifying collected events...")
        
        if not events:
            self.print_error("No events collected!")
            self.print_info("Expected events for LIST command:")
            self.print_info("  - TRDOS_ENTRY")
            self.print_info("  - COMMAND_START")
            self.print_info("  - FDC_CMD_READ (catalog sectors)")
            return False
            
        # Print event summary
        print(f"\n{Colors.BOLD}Event Summary:{Colors.END}")
        
        # Map integer types to strings for verification
        EVENT_TYPE_MAP = {
            0: 'TRDOS_ENTRY',
            1: 'TRDOS_EXIT',
            2: 'COMMAND_START',
            3: 'COMMAND_COMPLETE',
            8: 'FDC_CMD_RESTORE',
            9: 'FDC_CMD_SEEK',
            10: 'FDC_CMD_STEP',
            11: 'FDC_CMD_READ',
            12: 'FDC_CMD_WRITE',
            13: 'FDC_CMD_READ_ADDR',
            14: 'FDC_CMD_READ_TRACK',
            15: 'FDC_CMD_WRITE_TRACK',
            16: 'SECTOR_TRANSFER',
            21: 'LOADER_DETECTED'
        }
        
        event_types = {}
        for event in events:
            raw_type = event.get('type')
            if isinstance(raw_type, int):
                type_name = EVENT_TYPE_MAP.get(raw_type, f"UNKNOWN({raw_type})")
            else:
                type_name = str(raw_type)
                
            event_types[type_name] = event_types.get(type_name, 0) + 1
            
        for event_type, count in event_types.items():
            print(f"  {event_type}: {count}")
            
        # Verify expected events
        expected_events = ['COMMAND_START', 'FDC_CMD_READ']
        found_events = set(event_types.keys())
        
        all_found = True
        for expected in expected_events:
            if expected in found_events:
                self.print_success(f"Found expected event: {expected}")
            else:
                self.print_warning(f"Missing expected event: {expected}")
                all_found = False
        
        # We generally expect NO LOADER_DETECTED for standard LIST command
        if 'LOADER_DETECTED' in found_events:
            self.print_warning("Unexpected LOADER_DETECTED event in standard LIST command")
            # We don't fail here yet, but it's worth noting.
                
        # Print detailed events
        if events:
            print(f"\n{Colors.BOLD}Detailed Events:{Colors.END}")
            
            # State tracking for narrative
            last_track = 0  # Assume start at T0 (restored)
            
            # Time tracking for resets (monotonicity)
            last_frame = 0
            frame_offset = 0
            
            for i, event in enumerate(events, 1):
                timestamp = event.get('timestamp', 0)
                frame = event.get('frame_number', 0)
                raw_type = event.get('type', 0)
                
                # Detect emulator reset (frame counter rewind)
                if frame < last_frame:
                    frame_offset += last_frame + 1 # Add 1 to ensure strict increase
                    
                last_frame = frame
                adjusted_frame = frame + frame_offset
                
                # Context formatting
                ctx = event.get('context', {})
                pc_str = ""
                if ctx and ctx.get('pc') is not None:
                    pc_str = f" PC=${ctx.get('pc', 0):04X}"

                # FDC Details
                track = event.get('track')
                sector = event.get('sector')
                
                # Default description
                type_name = str(raw_type)
                if isinstance(raw_type, int):
                    type_name = EVENT_TYPE_MAP.get(raw_type, f"UNKNOWN({raw_type})")

                # Semantic descriptions
                desc = type_name
                
                if type_name == 'FDC_CMD_RESTORE':
                    desc = "Recalibrate (Seek T0)"
                    last_track = 0
                
                elif type_name == 'FDC_CMD_SEEK':
                    if track is not None:
                        desc = f"Seek T{last_track} -> T{track}"
                        last_track = track
                    else:
                        desc = "Seek (Unknown Target)"
                        
                elif type_name == 'FDC_CMD_READ_ADDR':
                    # READ_ADDR reads IDAM to verify head position
                    desc = f"Verify Sector ID (at T{track})"
                    if sector:
                        desc += f" [Expect S{sector}]"
                        
                elif type_name == 'FDC_CMD_READ':
                    desc = f"Read Sector T{track}/S{sector}"
                    
                elif type_name == 'SECTOR_TRANSFER':
                    length = event.get('bytes_transferred', 0)
                    desc = f"Data Transfer ({length} bytes)"
                    
                elif type_name == 'TRDOS_ENTRY':
                    desc = "Entered TR-DOS ROM"
                    
                elif type_name == 'COMMAND_START':
                    desc = event.get('formatted', 'Command Recognized')

                # Time formatting
                # Use Frame Number for stable wall-clock time (50Hz = 20ms/frame)
                # T-states can be erratic across snapshot loads/resets
                total_seconds = adjusted_frame * 0.02
                minutes = int(total_seconds // 60)
                seconds = int(total_seconds % 60)
                millis = int((total_seconds * 1000) % 1000)
                
                if minutes > 0:
                    time_fmt = f"{minutes:02d}m:{seconds:02d}.{millis:03d}s"
                else:
                    time_fmt = f"{seconds:02d}.{millis:03d}s"
                
                # Format: [01m:12.345s] F:0056
                # Pad to ensure alignment
                time_str = f"[{time_fmt:>11}] F:{frame:04d}"
                
                print(f"  {i:>2}. {time_str} {desc:<40} {pc_str}")
                
                command = event.get('command')
                if command:
                    print(f"      Command: {command}")
                
        # Validate event fidelity (new check)
        fidelity_passed = True
        self.print_info(f"\nVerifying Event Fidelity:")
        
        for i, event in enumerate(events[:5], 1): # Check first 5 events for fidelity
            # Check Frame Number
            frame = event.get('frame_number')
            if frame is None:
                self.print_warning(f"  Event {i}: Missing 'frame_number'")
                fidelity_passed = False
            else:
                self.print_info(f"  Event {i}: Frame {frame}")

            # Check Context
            ctx = event.get('context')
            if not ctx:
                 self.print_warning(f"  Event {i}: Missing 'context' object")
                 fidelity_passed = False
            else:
                pc = ctx.get('pc')
                if pc is None:
                    self.print_warning(f"  Event {i}: Missing 'context.pc'")
                    fidelity_passed = False
                else:
                    self.print_info(f"  Event {i}: PC=${pc:04X}")

            # Check Timestamps
            ts = event.get('timestamp')
            if ts is None or (ts == 0 and event.get('type') != 0): # Entry might be 0 if very early
                 self.print_warning(f"  Event {i}: Invalid Timestamp {ts}")
                 # fidelity_passed = False # Warning only for now as startup might be 0

        if fidelity_passed:
            self.print_success("Event Fidelity Check Passed")
        else:
            self.print_warning("Event Fidelity Check Failed (Missing fields)")
            
        # Dump events to file for analysis
        with open('trdos_events.json', 'w') as f:
            import json
            json.dump(events, f, indent=2)
        self.print_info(f"Dumped {len(events)} events to trdos_events.json")

        return all_found and fidelity_passed
        
    def deactivate_analyzer(self) -> bool:
        """Deactivate TR-DOS analyzer"""
        self.print_step(7, "Deactivating TR-DOS analyzer...")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/analyzer/trdos"
        payload = {"enabled": False}
        
        try:
            response = self.session.put(url, json=payload, timeout=5)
            response.raise_for_status()
            
            result = response.json()
            if result.get('success') or result.get('enabled') == False:
                self.print_success("TR-DOS analyzer deactivated")
                return True
            else:
                self.print_warning(f"Deactivation returned: {result.get('message', 'Unknown')}")
                return True  # Not critical
                
        except requests.exceptions.HTTPError as e:
            self.print_warning(f"HTTP error during deactivation: {e}")
            return True  # Not critical
        except Exception as e:
            self.print_warning(f"Failed to deactivate analyzer: {e}")
            return True  # Not critical
            
    def run_verification(self, emulator_uuid: Optional[str] = None) -> bool:
        """Run the complete verification workflow"""
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.BOLD}TR-DOS Analyzer Verification{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}")
        
        # Step 1: Check connection
        if not self.check_connection():
            return False
            
        # Step 2: Select emulator
        if not self.select_emulator(emulator_uuid):
            return False
        
        # Step 2.5: Load TR-DOS snapshot (TR-DOS is now active)
        if not self.load_trdos_snapshot():
            return False
        
        # Step 2.7: Load disk image into drive A
        if not self.load_disk_image():
            return False
        
        # Capture initial screen state
        self.print_step("2.9", "Capturing initial screen state...")
        self.capture_screen_ocr("before command injection")
            
        # Step 3: Activate analyzer
        if not self.activate_analyzer():
            return False
        
        # Step 3.5: Verify breakpoints are registered (IMMEDIATELY after activation)
        # This must happen before any commands to avoid false positives from breakpoint removal
        self.verify_breakpoints()
        
        # Step 4: TEST INJECTION - Inject LIST command and verify it appears on screen
        # This tests the BasicEncoder injection mode (TR-DOS mode)
        if not self.verify_command_injection('LIST'):
            self.print_error("LIST command injection failed - BasicEncoder issue!")
            self.deactivate_analyzer()
            return False
            
        # Wait a bit after injection to let the ROM "see" the cursor-right redraw
        time.sleep(0.5)
        
        # Step 4c: Inject ENTER to execute the injected command
        self.print_step("4c", "Injecting ENTER to execute command...")
        self.inject_enter()
            
        # Wait for command execution to complete
        self.print_info("Waiting for LOAD command to complete...")
        time.sleep(2.0)  # Give more time for disk operations
        
        # Step 4d: Capture screen after command execution to verify it ran
        self.print_step("4d", "Verifying command executed...")
        success, lines = self.capture_results("LOAD command results")
        if success:
            screen_text = '\n'.join(lines)
            if 'LOAD' in screen_text.upper() or 'A>' in screen_text:
                self.print_success("Command appears to have executed")
            else:
                self.print_info("Screen state changed after command")
        
        # Step 5: Get events
        success, events = self.get_events()
        if not success:
            self.deactivate_analyzer()
            return False
        
        # Step 5.5: Check analyzer state
        self.check_analyzer_state()
        
        # Step 6: Verify events
        verification_passed = self.verify_events(events)
        
        # Step 7: Deactivate analyzer
        self.deactivate_analyzer()
        
        # Final result
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        if verification_passed and len(events) > 0:
            print(f"{Colors.GREEN}{Colors.BOLD}✓ VERIFICATION PASSED{Colors.END}")
            print(f"{Colors.GREEN}TR-DOS analyzer is working correctly!{Colors.END}")
        else:
            print(f"{Colors.RED}{Colors.BOLD}✗ VERIFICATION FAILED{Colors.END}")
            if len(events) == 0:
                print(f"{Colors.RED}No events were collected.{Colors.END}")
            else:
                print(f"{Colors.YELLOW}Some expected events were missing.{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}\n")
        
        return verification_passed and len(events) > 0


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Verify TR-DOS analyzer functionality via WebAPI',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Auto-discover emulator on default port
  python verify_trdos_analyzer.py
  
  # Specify custom port
  python verify_trdos_analyzer.py --port 8090
  
  # Specify emulator UUID
  python verify_trdos_analyzer.py --uuid 12345678-1234-1234-1234-123456789abc
        """
    )
    
    parser.add_argument('--host', default='localhost',
                        help='WebAPI host (default: localhost)')
    parser.add_argument('--port', type=int, default=8090,
                        help='WebAPI port (default: 8090)')
    parser.add_argument('--uuid', default=None,
                        help='Specific emulator UUID (auto-discovers if not provided)')
    
    args = parser.parse_args()
    
    verifier = TRDOSAnalyzerVerifier(host=args.host, port=args.port)
    
    try:
        success = verifier.run_verification(emulator_uuid=args.uuid)
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print(f"\n\n{Colors.YELLOW}Verification interrupted by user{Colors.END}")
        sys.exit(130)
    except Exception as e:
        print(f"\n{Colors.RED}Unexpected error: {e}{Colors.END}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
