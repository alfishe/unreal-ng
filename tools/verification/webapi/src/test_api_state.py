"""State inspection API tests.

Actualized against the current /state response schemas (verified live):
- /state/memory        -> {model, paging, ram, rom}
- /state/memory/ram    -> {banks (bank0..bank3 page slots), model, paging_control}
- /state/memory/rom    -> {active_rom_page, mapping, model, pages,
                           port_7ffd_bit4_rom_select, rom_file, rom_size_kb,
                           total_rom_pages}
- /state/screen        -> {active_ram_page, active_screen, border_color,
                           display_mode, is_128k, model}
- /state/screen/mode   -> {resolution, video_mode, memory_layout, ...}
- /state/screen/flash  -> {flash_phase, flash_cycle_*, toggle_interval_*}
- /state/audio/ay      -> {available, available_chips, chips, turbo_sound, ...}
                          (chip registers map 16 descriptive names to values)
- /state/audio/beeper  -> {device, output_port, ...}
- /state/audio/channels-> {ay_channels, beeper, covox, general_sound, master}
"""


class TestStateInspection:

    def test_memory_state(self, api_client, active_emulator):
        """Verify memory state endpoints."""
        emu_id = active_emulator

        # Overview
        mem = api_client.get_memory_overview(emu_id)
        assert "model" in mem
        assert "paging" in mem
        assert "ram" in mem
        assert "rom" in mem

        # RAM
        ram = api_client.get_ram_state(emu_id)
        assert "banks" in ram
        assert "paging_control" in ram
        # banks maps the four 16K page slots (bank0..bank3) to their mapping
        assert isinstance(ram["banks"], dict)
        for slot in ("bank0", "bank1", "bank2", "bank3"):
            assert slot in ram["banks"]
            assert "address_range" in ram["banks"][slot]
            assert "type" in ram["banks"][slot]

        # ROM
        rom = api_client.get_rom_state(emu_id)
        assert "total_rom_pages" in rom
        assert "active_rom_page" in rom
        assert "rom_size_kb" in rom

    def test_screen_state(self, api_client, active_emulator):
        """Verify screen state endpoints."""
        emu_id = active_emulator

        # Overview
        screen = api_client.get_screen_state(emu_id)
        assert "display_mode" in screen
        assert "border_color" in screen
        assert "active_screen" in screen
        # The real video mode since f0ff08e50 (it was the literal "standard"): the fixture's Pentagon boots into the
        # standard ZX screen with Pentagon timing; display_mode is the legacy alias of video_mode
        assert screen["video_mode"] == "Pentagon128K"
        assert screen["display_mode"] == screen["video_mode"]

        # Mode details
        mode = api_client.get_screen_mode(emu_id)
        assert mode["video_mode"] == screen["video_mode"]
        assert mode["resolution"] == "256x192"
        assert "memory_layout" in mode
        assert mode["memory_layout"]["total_bytes"] == 6912

        # Flash phase
        flash = api_client.get_screen_flash(emu_id)
        assert flash["flash_phase"] in ("normal", "inverted")
        assert flash["flash_cycle_total"] > 0
        assert 0 <= flash["flash_cycle_position"] <= flash["flash_cycle_total"]

    def test_audio_state(self, api_client, active_emulator):
        """Verify audio state endpoints."""
        emu_id = active_emulator

        # AY chips overview
        ay_overview = api_client.get_ay_chips(emu_id)
        assert "available_chips" in ay_overview
        assert isinstance(ay_overview["available_chips"], int)

        if ay_overview["available_chips"] > 0:
            # Detail of first chip
            chip = api_client.get_ay_chip(emu_id, 0)
            assert "registers" in chip
            # registers maps 16 descriptive names to their current values
            assert isinstance(chip["registers"], dict)
            assert len(chip["registers"]) == 16
            for value in chip["registers"].values():
                assert isinstance(value, int)
                assert 0 <= value <= 255

            # Specific register
            reg = api_client.get_ay_register(emu_id, 0, 0)
            assert reg["register_number"] == 0
            assert 0 <= reg["value_dec"] <= 255

        # Beeper
        beeper = api_client.get_beeper_state(emu_id)
        assert "device" in beeper
        assert beeper["output_port"] == "0xFE"

        # Channels
        channels = api_client.get_audio_channels(emu_id)
        for group in ("ay_channels", "beeper", "covox", "general_sound", "master"):
            assert group in channels


class TestRomProtect:
    """GET/PUT /memory/rom/protect: one flag per instance (it was one for the whole server), 404 for an unknown id"""

    def _protect(self, api_client, emulator_id, value=None):
        url = api_client._url(f"/api/v1/emulator/{emulator_id}/memory/rom/protect")
        if value is None:
            return api_client.session.get(url)
        return api_client.session.put(url, json={"protected": value})

    def _write_rom(self, api_client, emulator_id):
        url = api_client._url(f"/api/v1/emulator/{emulator_id}/memory/page/rom/0")
        return api_client.session.post(url, json={"offset": "0x0000", "data": [0xF3]})

    def test_the_flag_belongs_to_one_instance(self, api_client, active_emulator):
        other = api_client.create_and_start_emulator(model="PENTAGON")["id"]
        try:
            assert self._protect(api_client, active_emulator).json()["protected"] is True
            assert self._protect(api_client, other).json()["protected"] is True

            resp = self._protect(api_client, active_emulator, False)
            assert resp.status_code == 200 and resp.json()["protected"] is False
            assert self._protect(api_client, other).json()["protected"] is True, "the other instance stays protected"

            assert self._write_rom(api_client, other).status_code == 403
            assert self._write_rom(api_client, active_emulator).status_code == 200

            self._protect(api_client, active_emulator, True)
            assert self._write_rom(api_client, active_emulator).status_code == 403
        finally:
            api_client.stop_emulator(other)
            api_client.delete_emulator(other)

    def test_an_unknown_instance_is_not_found(self, api_client):
        assert self._protect(api_client, "no-such-emulator").status_code == 404
        assert self._protect(api_client, "no-such-emulator", False).status_code == 404
