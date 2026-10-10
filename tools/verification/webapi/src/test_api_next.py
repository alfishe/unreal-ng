"""ZX Spectrum Next debugger reports and the NextREG write (design-automation-coverage.md).

Needs a running unreal-qt / unreal-cli WebAPI (UNREAL_API_URL, default http://localhost:8090) and a NEXT model that can start; the
test skips itself when the NEXT model is not creatable. The report values themselves are asserted by the core tests
(core/tests/emulator/io/z80n/nextreports_test.cpp); this file checks what only a live server shows: the routes exist, the query
parameters are parsed, the status codes are right and a write changes the machine.
"""

import time

import pytest


@pytest.fixture(scope="function")
def next_emulator(api_client):
    try:
        response = api_client.create_and_start_emulator(model="NEXT")
    except Exception as error:  # the model is not creatable in this build
        pytest.skip(f"NEXT is not creatable here: {error}")
    emulator_id = response["id"]
    time.sleep(0.5)
    yield emulator_id
    try:
        api_client.stop_emulator(emulator_id)
        api_client.delete_emulator(emulator_id)
    except Exception as error:
        print(f"Teardown failed for emulator {emulator_id}: {error}")


def get(api_client, emulator_id, path):
    return api_client.session.get(api_client._url(f"/api/v1/emulator/{emulator_id}{path}"))


def post(api_client, emulator_id, path, body):
    return api_client.session.post(api_client._url(f"/api/v1/emulator/{emulator_id}{path}"), json=body)


class TestNextReports:

    def test_every_report_answers_json(self, api_client, next_emulator):
        for path, member in (("/state/next/dma", "mode"), ("/state/next/video", "layer_order"), ("/state/next/palette", "palettes"),
                             ("/state/next/ports", "enable_word"), ("/state/next/nextreg", "registers")):
            response = get(api_client, next_emulator, path)
            assert response.status_code == 200, path
            assert member in response.json(), path

    def test_copper_and_sprites_reports(self, api_client, next_emulator):
        copper = get(api_client, next_emulator, "/state/next/copper?from=0&count=4&raw=true")
        assert copper.status_code == 200
        assert len(copper.json()["instructions"]) == 4
        assert len(copper.json()["raw"]) == 4096
        sprites = get(api_client, next_emulator, "/state/next/sprites?all=true&count=3")
        assert sprites.status_code == 200
        assert len(sprites.json()["sprites"]) == 3
        assert get(api_client, next_emulator, "/state/next/copper?count=0").status_code == 400
        assert get(api_client, next_emulator, "/state/next/sprites?from=128").status_code == 400

    def test_query_parameters_are_parsed(self, api_client, next_emulator):
        palette = get(api_client, next_emulator, "/state/next/palette?palette=sprites_1&range=0-3").json()
        assert palette["palettes"][0]["name"] == "sprites_1"
        assert len(palette["palettes"][0]["entries"]) == 4
        ports = get(api_client, next_emulator, "/state/next/ports?port=6B&access=w").json()
        assert ports["describe"]["device"] == "DMA (zxnDMA)"
        register = get(api_client, next_emulator, "/state/next/nextreg?reg=07").json()
        assert register["reg"] == "0x07"

    def test_bad_parameters_are_400(self, api_client, next_emulator):
        assert get(api_client, next_emulator, "/state/next/palette?palette=zz").status_code == 400
        assert get(api_client, next_emulator, "/state/next/ports?port=12345").status_code == 400
        assert get(api_client, next_emulator, "/state/next/nextreg?reg=100").status_code == 400

    def test_nextreg_write_lands_in_the_journal(self, api_client, next_emulator):
        assert post(api_client, next_emulator, "/next/reg-journal", {"enabled": True, "clear": True}).status_code == 200
        response = post(api_client, next_emulator, "/next/nextreg", {"reg": "14", "value": "12", "door": "nextreg"})
        assert response.status_code == 200, response.text
        assert response.json()["ok"] is True
        events = get(api_client, next_emulator, "/state/next/reg-journal?regs=14").json()["events"]
        assert events and events[-1]["source"] == "nextreg" and events[-1]["value"] == "0x12"
        assert get(api_client, next_emulator, "/state/next/nextreg?reg=14").json()["value"] == "0x12"

    def test_nextreg_write_bad_body_is_400(self, api_client, next_emulator):
        assert post(api_client, next_emulator, "/next/nextreg", {"reg": "14"}).status_code == 400
        assert post(api_client, next_emulator, "/next/nextreg", {"reg": "14", "value": "1", "door": "copper"}).status_code == 400

    def test_nextreg_write_on_another_machine_is_409(self, api_client):
        response = api_client.create_and_start_emulator(model="48K")
        emulator_id = response["id"]
        try:
            assert post(api_client, emulator_id, "/next/nextreg", {"reg": "14", "value": "1"}).status_code == 409
            assert get(api_client, emulator_id, "/state/next/dma").status_code == 404
        finally:
            api_client.stop_emulator(emulator_id)
            api_client.delete_emulator(emulator_id)
