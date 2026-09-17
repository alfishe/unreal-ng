"""Settings API tests.

Actualized against the current /settings contract (verified live):
- GET /settings            -> {emulator_id, settings} where settings contains
                              grouped entries (e.g. io_acceleration.fast_tape)
- GET /settings/{name}     -> {name, value, description, emulator_id};
                              booleans arrive as real JSON booleans
- PUT /settings/{name}     -> {name, value, message, emulator_id}; the request
                              body must carry a JSON-native value (bool/int/
                              string) - stringified booleans are rejected
- GET /settings/{unknown}  -> 404 {error: "Not Found", message: "Unknown setting: ..."}
"""


class TestSettings:

    def test_settings_listing(self, api_client, active_emulator):
        """Settings listing reports grouped entries with an emulator id."""
        emu_id = active_emulator

        settings = api_client.get_settings(emu_id)
        assert "emulator_id" in settings
        assert "settings" in settings
        assert isinstance(settings["settings"], dict)
        assert len(settings["settings"]) > 0

    def test_fast_tape_round_trip(self, api_client, active_emulator):
        """fast_tape is a boolean setting and toggles both ways."""
        emu_id = active_emulator

        current = api_client.get_setting(emu_id, "fast_tape")
        assert current["name"] == "fast_tape"
        assert isinstance(current["value"], bool)
        original = current["value"]

        try:
            api_client.update_setting(emu_id, "fast_tape", not original)
            flipped = api_client.get_setting(emu_id, "fast_tape")
            assert flipped["value"] is (not original)

            api_client.update_setting(emu_id, "fast_tape", original)
            restored = api_client.get_setting(emu_id, "fast_tape")
            assert restored["value"] is original
        finally:
            # Belt and braces: never leave fast_tape disabled for later tests
            api_client.update_setting(emu_id, "fast_tape", True)

    def test_unknown_setting_returns_404(self, api_client, active_emulator):
        """Reading or writing an unknown setting name fails with 404."""
        emu_id = active_emulator
        unknown = "definitely_not_a_setting"

        try:
            api_client.get_setting(emu_id, unknown)
            raise AssertionError("GET unknown setting should have failed")
        except Exception as e:
            assert "404" in str(e) or "Not Found" in str(e)

        try:
            api_client.update_setting(emu_id, unknown, True)
            raise AssertionError("PUT unknown setting should have failed")
        except Exception as e:
            assert "404" in str(e) or "Not Found" in str(e)
