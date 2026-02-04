#!/usr/bin/env python3
"""
48K BASIC Command Injection Verification Script

This script tests the ZX Spectrum 48K BASIC command injection functionality
via the UnrealSpeccy emulator's WebAPI. It verifies that:

1. Commands can be injected into the 48K BASIC editor buffer
2. The emulator correctly switches from 128K menu to 48K BASIC mode
3. Injected commands appear on the virtual screen (verified via OCR)

Test Flow:
----------
1. Connect to WebAPI (default: http://localhost:8090)
2. Find or create a running Pentagon emulator instance
3. Reset emulator to ensure clean 128K menu state
4. Verify menu is displayed via OCR
5. Switch to 48K BASIC mode using /basic/mode API
6. Inject test command (default: "PRINT 42")
7. Verify command appears on screen via OCR

Usage:
------
    python3 verify_48k_injection.py [command]
    
    Example:
        python3 verify_48k_injection.py "PRINT 42"
        python3 verify_48k_injection.py "10 LET A=5"

Requirements:
-------------
    - UnrealSpeccy emulator running with --webapi flag
    - requests library: pip install requests

Exit Codes:
-----------
    0 - Test passed (command injected and visible)
    1 - Test failed (connection error, injection failed, or command not visible)

Author: UnrealSpeccy Development Team
Date: 2026-01-22
"""

import requests
import time
import sys


class Basic48KInjectionTest:
    """
    Test class for 48K BASIC command injection verification.
    
    This class manages the complete lifecycle of a 48K injection test:
    - WebAPI connection management
    - Emulator instance discovery/creation
    - Mode switching (128K menu → 48K BASIC)
    - Command injection via BasicEncoder API
    - Screen verification via OCR
    
    Attributes:
        base_url (str): Base URL of the WebAPI server
        session (requests.Session): HTTP session for connection pooling
        emulator_uuid (str): UUID of the active emulator instance
    """
    
    def __init__(self, base_url="http://localhost:8090"):
        """
        Initialize the test instance.
        
        Args:
            base_url: WebAPI base URL (default: http://localhost:8090)
        """
        self.base_url = base_url
        self.session = requests.Session()
        self.emulator_uuid = None
        
    def print_step(self, step: str, msg: str):
        print(f"\n[Step {step}] {msg}")
        
    def print_success(self, msg: str):
        print(f"✓ {msg}")
        
    def print_error(self, msg: str):
        print(f"✗ {msg}")
        
    def print_info(self, msg: str):
        print(f"ℹ {msg}")

    def check_webapi(self) -> bool:
        """Check WebAPI is accessible"""
        self.print_step("1", "Checking WebAPI connection...")
        try:
            response = self.session.get(f"{self.base_url}/api/v1/emulator", timeout=5)
            if response.status_code == 200:
                self.print_success(f"WebAPI accessible at {self.base_url}")
                return True
        except Exception as e:
            self.print_error(f"Cannot connect to WebAPI: {e}")
        return False

    def ensure_running_emulator(self) -> bool:
        """Ensure we have a running emulator instance - create if needed"""
        self.print_step("2", "Checking for running emulator...")
        
        try:
            response = self.session.get(f"{self.base_url}/api/v1/emulator", timeout=5)
            if response.status_code != 200:
                self.print_error(f"Failed to query emulators: {response.status_code}")
                return False
            
            data = response.json()
            emulators = data.get('emulators', [])
            
            # Find a running emulator
            running_emulator = None
            for emu in emulators:
                if emu.get('is_running'):
                    running_emulator = emu
                    break
            
            if running_emulator:
                self.emulator_uuid = running_emulator.get('id')
                self.print_success(f"Using running emulator: {self.emulator_uuid[:8]}...")
                self.print_info(f"State: {running_emulator.get('state', 'unknown')}")
                return True
            
            # No running emulator found - create a new one
            self.print_info("No running emulator found, creating new PENTAGON instance...")
            return self.create_new_emulator()
            
        except Exception as e:
            self.print_error(f"Exception checking emulators: {e}")
        return False
    
    def create_new_emulator(self) -> bool:
        """Start a new emulator instance (auto-creates if needed)"""
        try:
            # Calling /start will auto-create a new instance and select it
            self.print_info("Starting new emulator instance...")
            response = self.session.post(
                f"{self.base_url}/api/v1/emulator/start",
                json={"model": "PENTAGON"},
                timeout=10
            )
            
            if response.status_code in [200, 201]:
                data = response.json()
                self.emulator_uuid = data.get('id')
                self.print_success(f"Started emulator: {self.emulator_uuid[:8] if self.emulator_uuid else 'unknown'}...")
                
                # Wait for emulator to initialize
                time.sleep(3)
                
                # Get the emulator ID if not returned
                if not self.emulator_uuid:
                    emu_response = self.session.get(f"{self.base_url}/api/v1/emulator", timeout=5)
                    if emu_response.status_code == 200:
                        emulators = emu_response.json().get('emulators', [])
                        for emu in emulators:
                            if emu.get('is_running'):
                                self.emulator_uuid = emu.get('id')
                                break
                
                if self.emulator_uuid:
                    self.print_success("Emulator is running")
                    return True
                else:
                    self.print_error("No running emulator found after start")
            else:
                self.print_error(f"Start failed: {response.status_code} - {response.text}")
                
        except Exception as e:
            self.print_error(f"Exception starting emulator: {e}")
        return False
    
    def reset_to_menu(self) -> bool:
        """Reset emulator to ensure it's in 128K menu state"""
        self.print_info("Resetting emulator to 128K menu...")
        
        try:
            url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/reset"
            response = self.session.post(url, timeout=10)
            
            if response.status_code == 200:
                self.print_success("Emulator reset complete")
                # Wait for menu to appear
                time.sleep(2)
                return True
            else:
                self.print_info(f"Reset returned: {response.status_code}")
                return True  # Continue anyway
                
        except Exception as e:
            self.print_info(f"Reset exception: {e}")
        return True  # Continue anyway
    
    def switch_to_48k_basic(self) -> bool:
        """Switch Pentagon to 48K BASIC mode using the /basic/mode API"""
        self.print_info("Switching to 48K BASIC mode...")
        
        try:
            # Use the new /basic/mode API endpoint
            url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/basic/mode"
            response = self.session.post(url, json={"mode": "48k"}, timeout=10)
            
            if response.status_code == 200:
                data = response.json()
                if data.get('success'):
                    self.print_success(f"Switched to 48K BASIC: {data.get('message')}")
                    # Wait for mode switch to complete (ROM processes ENTER keypress)
                    time.sleep(2)
                    return True
                else:
                    self.print_error(f"Mode switch failed: {data.get('message')}")
            else:
                self.print_error(f"Mode switch HTTP error: {response.status_code} - {response.text}")
                
        except Exception as e:
            self.print_error(f"Mode switch exception: {e}")
        
        return False

    def capture_screen_ocr(self, description: str = "") -> tuple:
        """Capture screen via OCR"""
        self.print_info(f"Screen OCR: {description}...")
        
        try:
            url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/capture/ocr"
            response = self.session.get(url, timeout=10)
            
            if response.status_code == 200:
                data = response.json()
                lines = data.get('lines', [])
                self.print_success(f"OCR captured ({len(lines)} lines)")
                return True, lines
            else:
                self.print_error(f"OCR failed: {response.status_code}")
                
        except Exception as e:
            self.print_error(f"OCR exception: {e}")
        return False, []

    def inject_command(self, command: str) -> bool:
        """Inject a command into the BASIC editor"""
        self.print_info(f"Injecting command: {command}")
        
        try:
            url = f"{self.base_url}/api/v1/emulator/{self.emulator_uuid}/basic/inject"
            response = self.session.post(url, json={"command": command}, timeout=10)
            
            if response.status_code == 200:
                data = response.json()
                if data.get('success'):
                    state = data.get('state', 'unknown')
                    self.print_success(f"Command injected (state: {state})")
                    return True
                else:
                    self.print_error(f"Injection failed: {data.get('message')}")
            else:
                self.print_error(f"HTTP error: {response.status_code} - {response.text}")
                
        except Exception as e:
            self.print_error(f"Injection exception: {e}")
        return False

    def verify_on_screen(self, command: str) -> bool:
        """Verify command appears on screen via OCR"""
        self.print_step("4", f"Verifying '{command}' visible on screen...")
        
        # Wait for screen refresh
        time.sleep(1.0)
        
        success, lines = self.capture_screen_ocr("after injection")
        if not success:
            return False
            
        # Display screen content
        print("\nScreen Content Preview:")
        for line in lines[:10]:
            if line.strip():
                print(f"  |{line}|")
        print()
        
        # Check if command appears
        screen_text = '\n'.join(lines).upper()
        command_upper = command.upper()
        
        if command_upper in screen_text:
            self.print_success(f"Command '{command}' visible on screen!")
            return True
        else:
            self.print_error(f"Command '{command}' NOT visible on screen!")
            return False

    def cleanup(self):
        """Delete the emulator instance - disabled for debugging"""
        # Skip cleanup to keep emulator for debugging
        self.print_info("Cleanup skipped - emulator preserved for debugging")
        pass

    def run_test(self, command: str = "PRINT 42") -> bool:
        """Run the complete injection test"""
        print("=" * 60)
        print("48K BASIC Command Injection Verification")
        print("=" * 60)
        
        try:
            if not self.check_webapi():
                return False
                
            if not self.ensure_running_emulator():
                return False
            
            # Reset to ensure we're in 128K menu
            self.print_step("2b", "Resetting emulator to 128K menu...")
            self.reset_to_menu()
            
            # Verify menu is displayed via OCR
            success, lines = self.capture_screen_ocr("after reset")
            screen_text = '\n'.join(lines).upper() if lines else ""
            if "128" in screen_text or "BASIC" in screen_text or "TAPE" in screen_text:
                self.print_success("Menu confirmed via OCR")
            else:
                self.print_info("Menu text not detected - continuing anyway")
            
            # Switch to 48K BASIC mode
            self.print_step("3", "Switching to 48K BASIC mode...")
            if not self.switch_to_48k_basic():
                return False
            
            # Capture screen after mode switch
            self.print_step("4", "Capturing screen state...")
            self.capture_screen_ocr("before injection")
            
            # Inject command
            self.print_step("5", f"Injecting command: {command}")
            if not self.inject_command(command):
                return False
            
            # Verify on screen
            if not self.verify_on_screen(command):
                return False
            
            print("\n" + "=" * 60)
            print("✓ 48K INJECTION TEST PASSED")
            print("=" * 60)
            return True
            
        finally:
            self.cleanup()


def main():
    test = Basic48KInjectionTest()
    
    # Test with PRINT command (common BASIC command)
    command = "PRINT 42" if len(sys.argv) < 2 else sys.argv[1]
    
    success = test.run_test(command)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
