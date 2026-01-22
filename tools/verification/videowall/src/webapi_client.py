#!/usr/bin/env python3
"""
WebAPI client helper for videowall testing.

Provides reusable methods for interacting with the emulator WebAPI,
designed to support multiple test scenarios.
"""

import os
import random
import requests
from typing import List, Dict, Optional, Any
from urllib.parse import urljoin


def find_project_root() -> str:
    """
    Find the project root by looking for characteristic files/directories.
    Starts from the script location and walks up the directory tree.
    """
    current = os.path.dirname(os.path.abspath(__file__))
    
    # Walk up until we find a root indicator
    while current != os.path.dirname(current):  # Stop at filesystem root
        # Check for indicators that we're at project root
        if os.path.exists(os.path.join(current, 'testdata')) and \
           os.path.exists(os.path.join(current, 'core')):
            return current
        current = os.path.dirname(current)
    
    raise RuntimeError("Could not find project root (looking for 'testdata' and 'core' directories)")


class WebAPIClient:
    """
    WebAPI client for emulator instance management.
    
    Provides methods for:
    - Listing emulator instances
    - Managing emulator state (start, stop, pause, resume)
    - Loading snapshots, disks, tapes
    - Configuring features (sound, video, debug mode)
    """
    
    def __init__(self, base_url: str = "http://localhost:8090"):
        """
        Initialize the WebAPI client.
        
        Args:
            base_url: Base URL of the WebAPI server (default: http://localhost:8090)
        """
        self.base_url = base_url.rstrip('/')
        self.session = requests.Session()
        self.timeout = 10
    
    def _url(self, path: str) -> str:
        """Build full URL from path."""
        return urljoin(self.base_url, path)
    
    def _get(self, path: str, **kwargs) -> requests.Response:
        """Execute a GET request."""
        return self.session.get(self._url(path), timeout=self.timeout, **kwargs)
    
    def _post(self, path: str, **kwargs) -> requests.Response:
        """Execute a POST request."""
        return self.session.post(self._url(path), timeout=self.timeout, **kwargs)
    
    def _put(self, path: str, **kwargs) -> requests.Response:
        """Execute a PUT request."""
        return self.session.put(self._url(path), timeout=self.timeout, **kwargs)
    
    def _delete(self, path: str, **kwargs) -> requests.Response:
        """Execute a DELETE request."""
        return self.session.delete(self._url(path), timeout=self.timeout, **kwargs)
    
    # ========== Server Health ==========
    
    def check_health(self) -> bool:
        """Check if the WebAPI server is reachable and responding."""
        try:
            response = self._get("/api/v1/emulator")
            return response.status_code == 200
        except requests.exceptions.RequestException:
            return False
    
    # ========== Emulator Instance Management ==========
    
    def list_emulators(self) -> List[Dict[str, Any]]:
        """
        Get list of all emulator instances.
        
        Returns:
            List of emulator info dictionaries with 'id', 'status', etc.
        """
        response = self._get("/api/v1/emulator")
        response.raise_for_status()
        data = response.json()
        emulators = data.get('emulators', [])
        # Map 'state' field to 'status' for backward compatibility
        for emu in emulators:
            if 'state' in emu and 'status' not in emu:
                emu['status'] = emu['state']
        return emulators
    
    def get_emulator_ids(self) -> List[str]:
        """Get list of emulator IDs only."""
        emulators = self.list_emulators()
        return [emu['id'] for emu in emulators]
    
    def get_emulator(self, emulator_id: str) -> Optional[Dict[str, Any]]:
        """Get details for a specific emulator."""
        response = self._get(f"/api/v1/emulator/{emulator_id}")
        if response.status_code == 200:
            emu = response.json()
            # Map 'state' field to 'status' for backward compatibility
            if 'state' in emu and 'status' not in emu:
                emu['status'] = emu['state']
            return emu
        return None
    
    def create_emulator(self, model: str = "48k") -> Optional[str]:
        """
        Create a new emulator instance (without starting it).
        
        Args:
            model: Emulator model (e.g., "48k", "128k", "pentagon")
            
        Returns:
            Emulator ID if successful, None otherwise
        """
        response = self._post("/api/v1/emulator", json={"model": model})
        if response.status_code in [200, 201]:
            data = response.json()
            return data.get('id')
        return None
    
    def start_emulator(self, emulator_id: str) -> bool:
        """Start a specific emulator instance."""
        response = self._post(f"/api/v1/emulator/{emulator_id}/start")
        return response.status_code == 200
    
    def stop_emulator(self, emulator_id: str) -> bool:
        """Stop a specific emulator instance."""
        response = self._post(f"/api/v1/emulator/{emulator_id}/stop")
        return response.status_code == 200
    
    def pause_emulator(self, emulator_id: str) -> bool:
        """Pause a specific emulator instance."""
        response = self._post(f"/api/v1/emulator/{emulator_id}/pause")
        return response.status_code == 200
    
    def resume_emulator(self, emulator_id: str) -> bool:
        """Resume a specific emulator instance."""
        response = self._post(f"/api/v1/emulator/{emulator_id}/resume")
        return response.status_code == 200
    
    # ========== Feature Management ==========
    
    def get_features(self, emulator_id: str) -> Optional[Dict[str, Any]]:
        """Get all features for an emulator."""
        response = self._get(f"/api/v1/emulator/{emulator_id}/features")
        if response.status_code == 200:
            return response.json()
        return None
    
    def set_feature(self, emulator_id: str, feature_name: str, enabled: bool) -> bool:
        """
        Enable or disable a feature on an emulator.
        
        Args:
            emulator_id: Target emulator ID
            feature_name: Name of the feature (e.g., "sound", "video")
            enabled: True to enable, False to disable
        """
        response = self._put(
            f"/api/v1/emulator/{emulator_id}/feature/{feature_name}",
            json={"enabled": enabled}
        )
        return response.status_code == 200
    
    def disable_sound(self, emulator_id: str) -> bool:
        """Disable sound for an emulator instance."""
        return self.set_feature(emulator_id, "sound", False)
    
    def enable_sound(self, emulator_id: str) -> bool:
        """Enable sound for an emulator instance."""
        return self.set_feature(emulator_id, "sound", True)
    
    # ========== Snapshot/File Loading ==========
    
    def load_snapshot(self, emulator_id: str, snapshot_path: str) -> bool:
        """
        Load a snapshot file into an emulator.
        
        Args:
            emulator_id: Target emulator ID
            snapshot_path: Absolute path to the snapshot file (.sna or .z80)
        """
        response = self._post(
            f"/api/v1/emulator/{emulator_id}/snapshot/load",
            json={"path": snapshot_path}
        )
        return response.status_code == 200
    
    def load_tape(self, emulator_id: str, tape_path: str) -> bool:
        """Load a tape file into an emulator."""
        response = self._post(
            f"/api/v1/emulator/{emulator_id}/tape/load",
            json={"path": tape_path}
        )
        return response.status_code == 200
    
    def load_disk(self, emulator_id: str, disk_path: str, drive: str = "A") -> bool:
        """Load a disk image into an emulator."""
        response = self._post(
            f"/api/v1/emulator/{emulator_id}/disk/{drive}/insert",
            json={"path": disk_path}
        )
        return response.status_code == 200


class TestDataHelper:
    """
    Helper for accessing test data files.

    Uses project root detection to locate testdata directory.
    Supports filtering snapshots via whitelist.
    """

    def __init__(self, whitelist_file: str = None):
        """
        Initialize TestDataHelper with optional whitelist filtering.

        Args:
            whitelist_file: Path to whitelist file containing allowed snapshot filenames.
                           Each line should contain one filename (with or without extension).
                           Lines starting with # are comments. Empty lines are ignored.
        """
        self.project_root = find_project_root()
        self.testdata_dir = os.path.join(self.project_root, "testdata")
        self.whitelist = self._load_whitelist(whitelist_file) if whitelist_file else None

    def _load_whitelist(self, whitelist_file: str) -> set:
        """
        Load whitelist from file.

        Args:
            whitelist_file: Path to whitelist file

        Returns:
            Set of allowed snapshot filenames (exact match with extension required)
        """
        allowed_snapshots = set()

        try:
            with open(whitelist_file, 'r', encoding='utf-8') as f:
                for line_num, line in enumerate(f, 1):
                    line = line.strip()
                    # Skip empty lines and comments
                    if not line or line.startswith('#'):
                        continue

                    # Require exact filename with extension
                    if '.' not in line:
                        raise RuntimeError(f"Line {line_num}: Filename must include extension: '{line}'")

                    allowed_snapshots.add(line)

        except FileNotFoundError:
            raise RuntimeError(f"Whitelist file not found: {whitelist_file}")
        except Exception as e:
            raise RuntimeError(f"Error loading whitelist file {whitelist_file}: {e}")

        return allowed_snapshots

    def _is_snapshot_allowed(self, snapshot_path: str) -> bool:
        """
        Check if a snapshot is allowed by the whitelist.

        Args:
            snapshot_path: Full path to snapshot file

        Returns:
            True if allowed (no whitelist or matches whitelist), False otherwise
        """
        if self.whitelist is None:
            return True  # No whitelist means all snapshots are allowed

        filename = os.path.basename(snapshot_path)

        return filename in self.whitelist
    
    def get_snapshots(self, extensions: List[str] = None) -> List[str]:
        """
        Get list of all snapshot files in testdata, filtered by whitelist if configured.

        Args:
            extensions: List of extensions to include (default: ['.sna', '.z80'])

        Returns:
            List of absolute paths to snapshot files (filtered by whitelist if set)
        """
        if extensions is None:
            extensions = ['.sna', '.z80']

        snapshots = []
        loaders_dir = os.path.join(self.testdata_dir, "loaders")

        for ext in extensions:
            ext_dir = os.path.join(loaders_dir, ext.lstrip('.'))
            if os.path.isdir(ext_dir):
                for filename in os.listdir(ext_dir):
                    if filename.lower().endswith(ext):
                        full_path = os.path.join(ext_dir, filename)
                        if self._is_snapshot_allowed(full_path):
                            snapshots.append(full_path)

        return snapshots
    
    def get_random_snapshot(self) -> str:
        """Get a random snapshot file path."""
        snapshots = self.get_snapshots()
        if not snapshots:
            if self.whitelist is not None:
                raise RuntimeError("No snapshot files found in testdata matching whitelist")
            else:
                raise RuntimeError("No snapshot files found in testdata")
        return random.choice(snapshots)
    
    def get_random_snapshots(self, count: int) -> List[str]:
        """
        Get multiple random snapshot file paths.

        If count > available snapshots, snapshots may repeat.
        """
        snapshots = self.get_snapshots()
        if not snapshots:
            if self.whitelist is not None:
                raise RuntimeError("No snapshot files found in testdata matching whitelist")
            else:
                raise RuntimeError("No snapshot files found in testdata")

        return [random.choice(snapshots) for _ in range(count)]
