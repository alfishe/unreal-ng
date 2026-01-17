import requests
import time
import logging

class UnrealApiClient:
    def __init__(self, base_url="http://localhost:8090"):
        self.base_url = base_url
        self.session = requests.Session()
        self.logger = logging.getLogger("UnrealApiClient")

    def _url(self, path):
        if not path.startswith("/"):
            path = "/" + path
        return f"{self.base_url}{path}"

    def _handle_response(self, response, expected_status=200):
        if isinstance(expected_status, int):
            expected_status = [expected_status]
            
        if response.status_code not in expected_status:
            self.logger.error(f"Request to {response.url} failed: {response.status_code} - {response.text}")
            # Try to parse error message if JSON
            try:
                error_data = response.json()
                raise Exception(f"API Error ({response.status_code}): {error_data}")
            except ValueError:
                raise Exception(f"API Error ({response.status_code}): {response.text}")
        
        try:
            return response.json()
        except ValueError:
            return response.text

    # --- Emulator Management ---
    def list_emulators(self):
        """GET /api/v1/emulator"""
        resp = self.session.get(self._url("/api/v1/emulator"))
        return self._handle_response(resp)

    def create_emulator(self, model="ZX48"):
        """POST /api/v1/emulator/create"""
        data = {"model": model}
        resp = self.session.post(self._url("/api/v1/emulator/create"), json=data)
        return self._handle_response(resp, expected_status=201)

    def get_status(self):
        """GET /api/v1/emulator/status"""
        resp = self.session.get(self._url("/api/v1/emulator/status"))
        return self._handle_response(resp)

    def get_models(self):
        """GET /api/v1/emulator/models"""
        resp = self.session.get(self._url("/api/v1/emulator/models"))
        return self._handle_response(resp)

    def get_emulator(self, emulator_id):
        """GET /api/v1/emulator/{id}"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}"))
        return self._handle_response(resp)

    def delete_emulator(self, emulator_id):
        """DELETE /api/v1/emulator/{id}"""
        resp = self.session.delete(self._url(f"/api/v1/emulator/{emulator_id}"))
        return self._handle_response(resp, expected_status=[200, 204])

    # --- Emulator Control ---
    def create_and_start_emulator(self, symbolic_id=None, model="ZX48", ram_size=None):
        """POST /api/v1/emulator/start"""
        data = {"model": model}
        if symbolic_id:
            data["symbolic_id"] = symbolic_id
        if ram_size:
            data["ram_size"] = ram_size
        resp = self.session.post(self._url("/api/v1/emulator/start"), json=data)
        return self._handle_response(resp, expected_status=201)

    def start_emulator(self, emulator_id):
        """POST /api/v1/emulator/{id}/start"""
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/start"))
        return self._handle_response(resp)

    def stop_emulator(self, emulator_id):
        """POST /api/v1/emulator/{id}/stop"""
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/stop"))
        return self._handle_response(resp)

    def pause_emulator(self, emulator_id):
        """POST /api/v1/emulator/{id}/pause"""
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/pause"))
        return self._handle_response(resp)

    def resume_emulator(self, emulator_id):
        """POST /api/v1/emulator/{id}/resume"""
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/resume"))
        return self._handle_response(resp)

    def reset_emulator(self, emulator_id):
        """POST /api/v1/emulator/{id}/reset"""
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/reset"))
        return self._handle_response(resp)

    # --- Tape Control ---
    def load_tape(self, emulator_id, file_path):
        """POST /api/v1/emulator/{id}/tape/load"""
        data = {"path": file_path}
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/tape/load"), json=data)
        return self._handle_response(resp)

    def eject_tape(self, emulator_id):
        """POST /api/v1/emulator/{id}/tape/eject"""
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/tape/eject"))
        return self._handle_response(resp)

    def play_tape(self, emulator_id):
        """POST /api/v1/emulator/{id}/tape/play"""
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/tape/play"))
        return self._handle_response(resp)

    def stop_tape(self, emulator_id):
        """POST /api/v1/emulator/{id}/tape/stop"""
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/tape/stop"))
        return self._handle_response(resp)

    def rewind_tape(self, emulator_id):
        """POST /api/v1/emulator/{id}/tape/rewind"""
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/tape/rewind"))
        return self._handle_response(resp)

    def get_tape_info(self, emulator_id):
        """GET /api/v1/emulator/{id}/tape/info"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/tape/info"))
        return self._handle_response(resp)

    # --- Disk Control ---
    def insert_disk(self, emulator_id, drive, file_path):
        """POST /api/v1/emulator/{id}/disk/{drive}/insert"""
        data = {"path": file_path}
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/insert"), json=data)
        return self._handle_response(resp)

    def create_disk(self, emulator_id, drive, cylinders=80, sides=2):
        """POST /api/v1/emulator/{id}/disk/{drive}/create - Create blank unformatted disk"""
        data = {"cylinders": cylinders, "sides": sides}
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/create"), json=data)
        return self._handle_response(resp)

    def eject_disk(self, emulator_id, drive):
        """POST /api/v1/emulator/{id}/disk/{drive}/eject"""
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/eject"))
        return self._handle_response(resp)

    def get_disk_info(self, emulator_id, drive):
        """GET /api/v1/emulator/{id}/disk/{drive}/info"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/info"))
        return self._handle_response(resp)

    # --- Disk Inspection ---
    def list_disk_drives(self, emulator_id):
        """GET /api/v1/emulator/{id}/disk - List all drives with status"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/disk"))
        return self._handle_response(resp)

    def read_disk_sector(self, emulator_id, drive, cylinder, side, sector):
        """GET /api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"""
        resp = self.session.get(
            self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/sector/{cylinder}/{side}/{sector}")
        )
        return self._handle_response(resp)

    def read_disk_sector_raw(self, emulator_id, drive, cylinder, side, sector):
        """GET /api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw"""
        resp = self.session.get(
            self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/sector/{cylinder}/{side}/{sector}/raw")
        )
        return self._handle_response(resp)

    def read_disk_track(self, emulator_id, drive, cylinder, side):
        """GET /api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"""
        resp = self.session.get(
            self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/track/{cylinder}/{side}")
        )
        return self._handle_response(resp)

    def read_disk_track_raw(self, emulator_id, drive, cylinder, side):
        """GET /api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw"""
        resp = self.session.get(
            self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/track/{cylinder}/{side}/raw")
        )
        return self._handle_response(resp)

    def get_disk_image(self, emulator_id, drive):
        """GET /api/v1/emulator/{id}/disk/{drive}/image - Full disk image dump"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/image"))
        return self._handle_response(resp)

    def get_disk_sysinfo(self, emulator_id, drive):
        """GET /api/v1/emulator/{id}/disk/{drive}/sysinfo - TR-DOS system sector"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/sysinfo"))
        return self._handle_response(resp)

    def get_disk_catalog(self, emulator_id, drive):
        """GET /api/v1/emulator/{id}/disk/{drive}/catalog - Disk file listing"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/disk/{drive}/catalog"))
        return self._handle_response(resp)

    # --- Snapshot Control ---
    def load_snapshot(self, emulator_id, file_path):
        """POST /api/v1/emulator/{id}/snapshot/load"""
        data = {"path": file_path}
        resp = self.session.post(self._url(f"/api/v1/emulator/{emulator_id}/snapshot/load"), json=data)
        return self._handle_response(resp)

    def get_snapshot_info(self, emulator_id):
        """GET /api/v1/emulator/{id}/snapshot/info"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/snapshot/info"))
        return self._handle_response(resp)

    # --- Settings Management ---
    def get_settings(self, emulator_id):
        """GET /api/v1/emulator/{id}/settings"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/settings"))
        return self._handle_response(resp)

    def get_setting(self, emulator_id, name):
        """GET /api/v1/emulator/{id}/settings/{name}"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/settings/{name}"))
        return self._handle_response(resp)

    def update_setting(self, emulator_id, name, value):
        """PUT /api/v1/emulator/{id}/settings/{name}"""
        data = {"value": value}
        resp = self.session.put(self._url(f"/api/v1/emulator/{emulator_id}/settings/{name}"), json=data)
        return self._handle_response(resp)

    # --- Memory State ---
    def get_memory_overview(self, emulator_id):
        """GET /api/v1/emulator/{id}/state/memory"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/memory"))
        return self._handle_response(resp)

    def get_ram_state(self, emulator_id):
        """GET /api/v1/emulator/{id}/state/memory/ram"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/memory/ram"))
        return self._handle_response(resp)

    def get_rom_state(self, emulator_id):
        """GET /api/v1/emulator/{id}/state/memory/rom"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/memory/rom"))
        return self._handle_response(resp)

    # --- Screen State ---
    def get_screen_state(self, emulator_id):
        """GET /api/v1/emulator/{id}/state/screen"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/screen"))
        return self._handle_response(resp)

    def get_screen_mode(self, emulator_id):
        """GET /api/v1/emulator/{id}/state/screen/mode"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/screen/mode"))
        return self._handle_response(resp)

    def get_screen_flash(self, emulator_id):
        """GET /api/v1/emulator/{id}/state/screen/flash"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/screen/flash"))
        return self._handle_response(resp)

    # --- Audio State ---
    def get_ay_chips(self, emulator_id):
        """GET /api/v1/emulator/{id}/state/audio/ay"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/audio/ay"))
        return self._handle_response(resp)

    def get_ay_chip(self, emulator_id, chip_index):
        """GET /api/v1/emulator/{id}/state/audio/ay/{chip}"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/audio/ay/{chip_index}"))
        return self._handle_response(resp)

    def get_ay_register(self, emulator_id, chip_index, reg_index):
        """GET /api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/audio/ay/{chip_index}/register/{reg_index}"))
        return self._handle_response(resp)

    def get_beeper_state(self, emulator_id):
        """GET /api/v1/emulator/{id}/state/audio/beeper"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/audio/beeper"))
        return self._handle_response(resp)

    def get_gs_state(self, emulator_id):
        """GET /api/v1/emulator/{id}/state/audio/gs"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/audio/gs"))
        return self._handle_response(resp)

    def get_covox_state(self, emulator_id):
        """GET /api/v1/emulator/{id}/state/audio/covox"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/audio/covox"))
        return self._handle_response(resp)

    def get_audio_channels(self, emulator_id):
        """GET /api/v1/emulator/{id}/state/audio/channels"""
        resp = self.session.get(self._url(f"/api/v1/emulator/{emulator_id}/state/audio/channels"))
        return self._handle_response(resp)

    # --- Python Interpreter ---
    def exec_python(self, code):
        """POST /api/v1/python/exec"""
        data = {"code": code}
        resp = self.session.post(self._url("/api/v1/python/exec"), json=data)
        return self._handle_response(resp)
    
    def exec_python_file(self, path):
        """POST /api/v1/python/file"""
        data = {"path": path}
        resp = self.session.post(self._url("/api/v1/python/file"), json=data)
        return self._handle_response(resp)

    def get_python_status(self):
        """GET /api/v1/python/status"""
        resp = self.session.get(self._url("/api/v1/python/status"))
        return self._handle_response(resp)

    def stop_python(self):
        """POST /api/v1/python/stop"""
        resp = self.session.post(self._url("/api/v1/python/stop"))
        return self._handle_response(resp)

    # --- Lua Interpreter ---
    def exec_lua(self, code):
        """POST /api/v1/lua/exec"""
        data = {"code": code}
        resp = self.session.post(self._url("/api/v1/lua/exec"), json=data)
        return self._handle_response(resp)

    def exec_lua_file(self, path):
        """POST /api/v1/lua/file"""
        data = {"path": path}
        resp = self.session.post(self._url("/api/v1/lua/file"), json=data)
        return self._handle_response(resp)

    def get_lua_status(self):
        """GET /api/v1/lua/status"""
        resp = self.session.get(self._url("/api/v1/lua/status"))
        return self._handle_response(resp)

    def stop_lua(self):
        """POST /api/v1/lua/stop"""
        resp = self.session.post(self._url("/api/v1/lua/stop"))
        return self._handle_response(resp)
