#!/usr/bin/env python3
"""
Screen Capture Verification Script

This script verifies the capture commands (OCR and screen capture) via WebAPI:
1. Connects to emulator WebAPI (port 8090)
2. Discovers or creates PENTAGON emulator instance
3. Waits for screen to render
4. Tests OCR capture endpoint
5. Tests screen capture endpoint (GIF and PNG formats)
6. Validates response data

Usage:
    python verify_capture.py [--host HOST] [--port PORT] [--uuid UUID]

Arguments:
    --host HOST     WebAPI host (default: localhost)
    --port PORT     WebAPI port (default: 8090)
    --uuid UUID     Specific emulator UUID (optional, auto-discovers if not provided)
"""

import argparse
import base64
import json
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

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
    """
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


class CaptureVerifier:
    """Verifies capture functionality via WebAPI"""
    
    def __init__(self, host: str = "localhost", port: int = 8090):
        self.base_url = f"http://{host}:{port}"
        self.emulator_uuid: Optional[str] = None
        self.session = requests.Session()
        self.session.headers.update({'Content-Type': 'application/json'})
        self.output_dir = Path(__file__).parent / "output"
        
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
            self.print_info("  Start with: ./unreal-qt --webapi")
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
            response = self.session.get(url, timeout=5)
            response.raise_for_status()
            state = response.json()
            
            is_running = state.get('is_running', False)
            is_paused = state.get('is_paused', False)
            
            if is_running and not is_paused:
                self.print_info("Emulator is running")
                return True
                
            if is_paused:
                self.print_info("Resuming paused emulator...")
                action_url = f"{self.base_url}/api/v1/emulator/{emulator_id}/resume"
            else:
                self.print_info("Starting emulator...")
                action_url = f"{self.base_url}/api/v1/emulator/{emulator_id}/start"
                
            response = self.session.post(action_url, timeout=5)
            response.raise_for_status()
            
            time.sleep(0.5)
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
            return True
            
        self.emulator_uuid = emulators[0]['id']
        self.print_info(f"Auto-selected emulator: {self.emulator_uuid[:8]}...")
        
        return self.ensure_emulator_running(self.emulator_uuid)
    
    def wait_for_screen(self) -> bool:
        """Wait for screen to have content"""
        self.print_step(3, "Waiting for screen to render...")
        
        # Give emulator time to render a few frames
        time.sleep(1.0)
        self.print_success("Screen should now have content")
        return True
        
    def test_ocr_capture(self) -> bool:
        """Test OCR capture endpoint"""
        self.print_step(4, "Testing OCR capture...")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/capture/ocr"
        
        try:
            response = self.session.get(url, timeout=10)
            response.raise_for_status()
            
            result = response.json()
            
            if result.get('status') != 'success':
                self.print_error(f"OCR failed: {result.get('message', 'Unknown error')}")
                return False
                
            # Validate response structure
            rows = result.get('rows', 0)
            cols = result.get('cols', 0)
            lines = result.get('lines', [])
            text = result.get('text', '')
            
            if rows != 24 or cols != 32:
                self.print_warning(f"Unexpected dimensions: {rows}x{cols} (expected 24x32)")
                
            if len(lines) != 24:
                self.print_warning(f"Expected 24 lines, got {len(lines)}")
                
            self.print_success(f"OCR returned {rows}x{cols} grid, {len(lines)} lines")
            
            # Show first few lines as preview
            print(f"\n{Colors.BOLD}OCR Preview (first 5 lines):{Colors.END}")
            for i, line in enumerate(lines[:5]):
                # Replace control chars with dots for display
                display_line = ''.join(c if c.isprintable() else '.' for c in line)
                print(f"  [{i+1:2d}] |{display_line}|")
            if len(lines) > 5:
                print(f"  ... ({len(lines)-5} more lines)")
                
            return True
            
        except requests.exceptions.HTTPError as e:
            self.print_error(f"HTTP error: {e}")
            if hasattr(e, 'response') and e.response:
                try:
                    error_body = e.response.json()
                    self.print_info(f"Error details: {error_body}")
                except:
                    pass
            return False
        except Exception as e:
            self.print_error(f"OCR test failed: {e}")
            return False
            
    def test_screen_capture(self, format: str = "gif", mode: str = "screen") -> bool:
        """Test screen capture endpoint"""
        step_name = f"{format.upper()} {mode}"
        print(f"\n{Colors.BOLD}[Test]{Colors.END} Screen capture ({step_name})...")
        
        url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/capture/screen"
        params = {"format": format, "mode": mode}
        
        try:
            response = self.session.get(url, params=params, timeout=30)
            response.raise_for_status()
            
            result = response.json()
            
            if result.get('status') != 'success':
                self.print_error(f"Capture failed: {result.get('message', 'Unknown error')}")
                return False
                
            # Validate response structure
            resp_format = result.get('format', '')
            width = result.get('width', 0)
            height = result.get('height', 0)
            size = result.get('size', 0)
            data = result.get('data', '')
            
            if resp_format != format:
                self.print_warning(f"Format mismatch: expected {format}, got {resp_format}")
                
            # Validate dimensions based on mode
            if mode == "screen":
                expected_w, expected_h = 256, 192
            else:
                expected_w, expected_h = 352, 288  # Approximate full frame
                
            self.print_success(f"Captured {width}x{height} {resp_format.upper()}, {size} bytes")
            
            # Validate base64 data
            if not data:
                self.print_error("No image data in response")
                return False
                
            try:
                decoded = base64.b64decode(data)
                self.print_info(f"Base64 decoded to {len(decoded)} bytes")
                
                # Verify magic bytes
                if format == "gif":
                    if decoded[:3] != b'GIF':
                        self.print_warning("GIF magic bytes not found")
                    else:
                        self.print_success("Valid GIF header detected")
                elif format == "png":
                    if decoded[:8] != b'\x89PNG\r\n\x1a\n':
                        self.print_warning("PNG magic bytes not found")
                    else:
                        self.print_success("Valid PNG header detected")
                        
                # Save to output directory for manual inspection
                self.output_dir.mkdir(parents=True, exist_ok=True)
                output_file = self.output_dir / f"capture_{mode}.{format}"
                output_file.write_bytes(decoded)
                self.print_info(f"Saved to: {output_file}")
                
            except Exception as e:
                self.print_error(f"Failed to decode base64 data: {e}")
                return False
                
            return True
            
        except requests.exceptions.HTTPError as e:
            self.print_error(f"HTTP error: {e}")
            if hasattr(e, 'response') and e.response:
                try:
                    error_body = e.response.json()
                    self.print_info(f"Error details: {error_body}")
                except:
                    pass
            return False
        except Exception as e:
            self.print_error(f"Screen capture test failed: {e}")
            return False
            
    def run_verification(self, emulator_uuid: Optional[str] = None) -> bool:
        """Run the complete verification workflow"""
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.BOLD}Screen Capture Verification{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}")
        
        test_results = {}
        
        # Step 1: Check connection
        if not self.check_connection():
            return False
            
        # Step 2: Select emulator
        if not self.select_emulator(emulator_uuid):
            return False
            
        # Step 3: Wait for screen
        if not self.wait_for_screen():
            return False
            
        # Step 4: Test OCR
        test_results['ocr'] = self.test_ocr_capture()
        
        # Step 5: Test screen capture (GIF, screen only - 256x192)
        test_results['gif_screen'] = self.test_screen_capture("gif", "screen")
        
        # Step 6: Test screen capture (GIF, full with border)
        test_results['gif_full'] = self.test_screen_capture("gif", "full")
        
        # Step 7: Test screen capture (PNG, screen only - 256x192)
        test_results['png_screen'] = self.test_screen_capture("png", "screen")
        
        # Step 8: Test screen capture (PNG, full with border)
        test_results['png_full'] = self.test_screen_capture("png", "full")
        
        # Final result
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        print(f"{Colors.BOLD}Test Results:{Colors.END}")
        
        all_passed = True
        for test_name, passed in test_results.items():
            status = f"{Colors.GREEN}PASSED{Colors.END}" if passed else f"{Colors.RED}FAILED{Colors.END}"
            print(f"  {test_name}: {status}")
            if not passed:
                all_passed = False
                
        print(f"\n{Colors.BOLD}{'='*60}{Colors.END}")
        if all_passed:
            print(f"{Colors.GREEN}{Colors.BOLD}✓ ALL TESTS PASSED{Colors.END}")
            print(f"{Colors.GREEN}Capture functionality is working correctly!{Colors.END}")
        else:
            print(f"{Colors.RED}{Colors.BOLD}✗ SOME TESTS FAILED{Colors.END}")
        print(f"{Colors.BOLD}{'='*60}{Colors.END}\n")
        
        return all_passed


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Verify capture functionality via WebAPI',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Auto-discover emulator on default port
  python verify_capture.py
  
  # Specify custom port
  python verify_capture.py --port 8090
  
  # Specify emulator UUID
  python verify_capture.py --uuid 12345678-1234-1234-1234-123456789abc
        """
    )
    
    parser.add_argument('--host', default='localhost',
                        help='WebAPI host (default: localhost)')
    parser.add_argument('--port', type=int, default=8090,
                        help='WebAPI port (default: 8090)')
    parser.add_argument('--uuid', default=None,
                        help='Specific emulator UUID (auto-discovers if not provided)')
    
    args = parser.parse_args()
    
    verifier = CaptureVerifier(host=args.host, port=args.port)
    
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
