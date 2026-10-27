"""ZX Spectrum Next debugger reports and the NextREG write (design-automation-coverage.md).

Needs a running unreal-qt / unreal-cli WebAPI (UNREAL_API_URL, default http://localhost:8090) and a NEXT model that can start; the
test skips itself when the NEXT model is not creatable. The report values themselves are asserted by the core tests
(core/tests/emulator/io/z80n/nextreports_test.cpp); this file checks what only a live server shows: the routes exist, the query
parameters are parsed, the status codes are right and a write changes the machine.
"""

import base64
import struct
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

    def test_sd_report_and_journal_switch(self, api_client, next_emulator):
        report = get(api_client, next_emulator, "/state/next/sd").json()
        assert len(report["cards"]) == 2 and "too_fast_accesses" in report["spi"]
        assert post(api_client, next_emulator, "/next/sd-journal", {"enabled": True, "clear": True}).json()["enabled"] is True
        assert get(api_client, next_emulator, "/state/next/sd?kinds=command&limit=5").json()["enabled"] is True
        assert post(api_client, next_emulator, "/next/sd-journal", {"enabled": False}).json()["enabled"] is False

    def test_sd_bad_parameters_are_400(self, api_client, next_emulator):
        assert get(api_client, next_emulator, "/state/next/sd?kinds=jump").status_code == 400
        assert get(api_client, next_emulator, "/state/next/sd?card=2").status_code == 400

    def test_divmmc_report_and_journal_switch(self, api_client, next_emulator):
        report = get(api_client, next_emulator, "/state/next/divmmc").json()
        assert len(report["entries"]) == 15 and "automap_latch" in report["state"]
        assert post(api_client, next_emulator, "/next/divmmc-journal", {"enabled": True, "clear": True}).json()["enabled"] is True
        assert get(api_client, next_emulator, "/state/next/divmmc?kinds=esx_call&limit=5").json()["enabled"] is True
        assert post(api_client, next_emulator, "/next/divmmc-journal", {"enabled": False}).json()["enabled"] is False

    def test_divmmc_bad_parameters_are_400(self, api_client, next_emulator):
        assert get(api_client, next_emulator, "/state/next/divmmc?kinds=jump").status_code == 400

    def test_nextreg_beam_and_copper_breakpoints(self, api_client, next_emulator):
        def add(body):
            return post(api_client, next_emulator, "/breakpoints", body)

        written = add({"type": "nextreg_write", "address": "0x40", "address_end": "0x44", "doors": "copper,port", "value": 1, "value_mask": 3})
        assert written.status_code == 201, written.text
        info = written.json()
        assert info["address"] == 0x40 and info["address_end"] == 0x44
        assert info["doors"] == "port,copper" and info["value"] == 1 and info["value_mask"] == 3
        beam = add({"type": "beam", "address": 99, "hc": 255})
        assert beam.status_code == 201, beam.text
        assert beam.json()["hc"] == 255
        copper = add({"type": "copper", "address": 12, "address_end": 14})
        assert copper.status_code == 201, copper.text
        listed = {b["type"]: b for b in get(api_client, next_emulator, "/breakpoints").json()["breakpoints"]}
        assert {"nextreg", "beam", "copper"} <= set(listed)
        assert listed["nextreg"]["write"] is True and listed["nextreg"]["read"] is False
        # a tool's write is no access of the program
        wrote = post(api_client, next_emulator, "/next/nextreg", {"reg": "0x40", "value": "0x01"})
        assert wrote.status_code == 200, wrote.text
        assert get(api_client, next_emulator, "/breakpoints").json()["breakpoints"][0]["hit_count"] == 0
        # refused: a read through the copper's door only, a register past 255, a pixel clock on another kind, an unknown door
        assert add({"type": "nextreg_read", "address": 7, "doors": "copper"}).status_code == 400
        assert add({"type": "nextreg_write", "address": 256}).status_code == 400
        assert add({"type": "nextreg_write", "address": 7, "hc": 3}).status_code == 400
        assert add({"type": "nextreg_write", "address": 7, "doors": "dma"}).status_code == 400
        assert add({"type": "beam", "address": 512}).status_code == 400
        assert add({"type": "copper", "address": 1024}).status_code == 400
        api_client.session.delete(api_client._url(f"/api/v1/emulator/{next_emulator}/breakpoints"))

    def test_uart_report_and_peers(self, api_client, next_emulator):
        report = get(api_client, next_emulator, "/state/next/uart").json()
        assert report["available"] is True and [c["name"] for c in report["channels"]] == ["esp", "pi"]
        esp, pi = report["channels"]
        assert esp["frame"]["data_bits"] == 8 and esp["frame"]["parity"] == "N" and esp["rx"]["capacity"] == 512 and esp["tx"]["capacity"] == 64
        assert esp["peer"]["attached"] is True  # the shipped ESP module (AT firmware)
        assert pi["peer"]["attached"] is False
        # a loopback on the Pi's line: ComPort2=
        changed = post(api_client, next_emulator, "/network/config", {"com_port2": "loopback"})
        assert changed.status_code == 200, changed.text
        deadline = time.time() + 5  # a change of the lines is taken at a frame boundary
        while time.time() < deadline:
            pi = get(api_client, next_emulator, "/state/next/uart").json()["channels"][1]
            if pi["peer"]["attached"]:
                break
            time.sleep(0.05)
        assert pi["peer"]["attached"] is True and pi["peer"]["kind"] == "loopback"

    def test_extended_keys_follow_the_hosts_keys(self, api_client, next_emulator):
        report = get(api_client, next_emulator, "/state/next/input").json()
        assert report["extended_keys"]["pressed"] == [] and report["md_pad_extra"]["nr_b2"] == "0x00"
        # the automation presses the ZX key "up"; the Next's keyboard controller hears the physical key too (NR #B0 bit 3)
        assert post(api_client, next_emulator, "/keyboard/press", {"key": "up"}).status_code == 200
        deadline = time.time() + 5
        while time.time() < deadline:
            report = get(api_client, next_emulator, "/state/next/input").json()
            if "up" in report["extended_keys"]["pressed"]:
                break
            time.sleep(0.1)
        assert "up" in report["extended_keys"]["pressed"], report["extended_keys"]
        assert int(report["extended_keys"]["nr_b0"], 16) & 0x08
        post(api_client, next_emulator, "/keyboard/release_all", {})

    def test_the_pads_extra_buttons_come_from_the_physical_keys_the_keyboard_api_presses(self, api_client, next_emulator):
        # the keyboard API takes PC key names too (journaled like a host key): keypad 3 / 9 / 1 / 7 are the host pad's MODE / Y / Z / X
        for key in ("kp_3", "kp_7"):
            assert post(api_client, next_emulator, "/keyboard/press", {"key": key}).status_code == 200
        deadline = time.time() + 5
        while time.time() < deadline:
            pad = get(api_client, next_emulator, "/state/next/input").json()["md_pad_extra"]
            if pad["left"]["mode"] and pad["left"]["x"]:
                break
            time.sleep(0.1)
        assert pad["left"] == {"mode": True, "x": True, "y": False, "z": False}, pad
        assert pad["right"] == {"mode": False, "x": False, "y": False, "z": False}, "the right connector has no host input"
        assert int(pad["nr_b2"], 16) == 0x09
        post(api_client, next_emulator, "/keyboard/release_all", {})

    def test_the_joystick_verbs_reach_the_right_connector_and_the_pads_buttons(self, api_client, next_emulator):
        pressed = post(api_client, next_emulator, "/joystick/press", {"buttons": "right.up+right.fire+x+mode"})
        assert pressed.status_code == 200, pressed.text
        # the input is applied on the machine's thread: read the report until the buttons are there
        deadline = time.time() + 5
        while time.time() < deadline:
            report = get(api_client, next_emulator, "/state/next/input").json()
            if sorted(report["right_pad"]["buttons"]) == ["fire", "up"] and report["md_pad_extra"]["left"]["x"]:
                break
            time.sleep(0.1)
        assert sorted(report["right_pad"]["buttons"]) == ["fire", "up"], report["right_pad"]
        assert report["md_pad_extra"]["left"]["x"] and report["md_pad_extra"]["left"]["mode"]
        status = get(api_client, next_emulator, "/joystick").json()
        assert sorted(status["left_extras"]) == ["mode", "x"] and sorted(status["right_pad"]["buttons"]) == ["fire", "up"]
        assert int(report["md_pad_extra"]["nr_b2"], 16) == 0x09  # the left pad's X (bit 3) and MODE (bit 0); the right pad has no extras
        assert post(api_client, next_emulator, "/joystick/press", {"buttons": "right.turbo"}).status_code == 400
        cleared = post(api_client, next_emulator, "/joystick/set", {"buttons": "right.start"})
        assert cleared.status_code == 200, cleared.text
        deadline = time.time() + 5
        while time.time() < deadline:
            report = get(api_client, next_emulator, "/state/next/input").json()
            if report["right_pad"]["buttons"] == ["start"]:
                break
            time.sleep(0.1)
        assert report["right_pad"]["buttons"] == ["start"] and report["md_pad_extra"]["left"] == {"mode": False, "x": False, "y": False, "z": False}

    def test_ram_sizes_a_4mb_request_is_created_as_2mb_with_a_note(self, api_client):
        models = api_client.session.get(api_client._url("/api/v1/emulator/models")).json()["models"]
        nxt = [m for m in models if m["name"] == "NEXT"][0]
        assert nxt["available_ram_sizes_kb"] == [1024, 2048], "4096 is not an offered size (no menu entry)"
        refused = api_client.session.post(api_client._url("/api/v1/emulator/start"), json={"model": "NEXT", "ram_size": 512})
        assert refused.status_code == 400, refused.text
        created = api_client.session.post(api_client._url("/api/v1/emulator/start"), json={"model": "NEXT", "ram_size": 4096})
        assert created.status_code == 201, created.text
        body = created.json()
        assert "2048" in body["note"] and "4096" in body["note"], body
        emulator_id = body["id"]
        try:
            info = api_client.session.get(api_client._url(f"/api/v1/emulator/{emulator_id}")).json()
            assert info["ram_kb"] == 2048
        finally:
            api_client.stop_emulator(emulator_id)
            api_client.delete_emulator(emulator_id)

    def test_magic_opcode_breakpoints(self, api_client, next_emulator):
        def add(body):
            return post(api_client, next_emulator, "/breakpoints", body)

        # the opcode is the type; the listing names it
        for kind, word in (("magic_ed_ff", 0xEDFF), ("magic_dd_01", 0xDD01)):
            added = add({"type": kind})
            assert added.status_code == 201, added.text
            assert added.json()["type"] == kind and added.json()["address"] == word
        listed = [b for b in get(api_client, next_emulator, "/breakpoints").json()["breakpoints"] if b["type"] == "magic"]
        assert {b["address"] for b in listed} == {0xEDFF, 0xDD01}
        assert add({"type": "magic_ed_ff", "address_end": 3}).status_code == 400
        api_client.session.delete(api_client._url(f"/api/v1/emulator/{next_emulator}/breakpoints"))

    def test_magic_port_console(self, api_client, next_emulator):
        report = get(api_client, next_emulator, "/state/next/magic-port").json()
        assert report["enabled"] is False and report["items"] == []
        on = post(api_client, next_emulator, "/next/magic-port", {"enabled": True, "port": 0x00FF, "mode": "line", "clear": True})
        assert on.status_code == 200, on.text
        assert on.json()["port"] == "0x00FF" and on.json()["mode"] == "line"
        assert post(api_client, next_emulator, "/next/magic-port", {"mode": "binary"}).status_code == 400
        assert post(api_client, next_emulator, "/next/magic-port", {"port": 70000}).status_code == 400
        assert get(api_client, next_emulator, "/state/next/magic-port?since=x").status_code == 400
        post(api_client, next_emulator, "/next/magic-port", {"enabled": False})

    def test_nmi_report_journal_and_trigger(self, api_client, next_emulator):
        report = get(api_client, next_emulator, "/state/next/nmi").json()
        names = [source["name"] for source in report["sources"]]
        assert names == ["m1_button", "drive_button", "io_trap"]
        assert report["sources"][2]["modelled"] is True and report["sources"][2]["cause"] == "none"
        assert {"in_service", "multiface", "nr_02", "stackless_nmi", "reset", "interrupts", "events"} <= set(report)
        journal = post(api_client, next_emulator, "/next/nmi-journal", {"enabled": True, "clear": True}).json()
        assert journal["enabled"] is True and journal["size"] == 0
        # the M1 button is off at power on (NR #06 bit 3): a write of NR #02 bit 3 is refused, with the reason, and the journal says so
        wrote = post(api_client, next_emulator, "/next/nextreg", {"reg": "02", "value": "08"})
        assert wrote.status_code == 200, wrote.text
        # the machine's "magic button" (POST /nmi {"magic": true}) is the DRIVE button on a Next; a plain POST /nmi is the bare /NMI line
        nmi = post(api_client, next_emulator, "/nmi", {"magic": True})
        assert nmi.status_code == 200, nmi.text
        events = get(api_client, next_emulator, "/state/next/nmi?kinds=nmi_request").json()["events"]
        assert [e["source"] for e in events][:2] == ["m1_button", "drive_button"], events
        assert events[0]["accepted"] is False and "NR #06" in events[0]["reason"]
        assert get(api_client, next_emulator, "/state/next/nmi?kinds=jump").status_code == 400
        post(api_client, next_emulator, "/next/nmi-journal", {"enabled": False})

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


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_header(data):
    """(width, height, bit depth, colour type) of a PNG; colour type 6 = RGBA"""
    assert data[:8] == PNG_SIGNATURE, "not a PNG"
    width, height, depth, colour = struct.unpack(">IIBB", data[16:26])
    return width, height, depth, colour


def wait_for_layer_frame(api_client, emulator_id, layer="ula", seconds=5.0):
    """The layer record answers 409 no-frame until a frame has been drawn; poll until it does"""
    deadline = time.time() + seconds
    while True:
        response = get(api_client, emulator_id, f"/capture/screen?layer={layer}")
        if response.status_code == 200 or time.time() > deadline:
            return response
        time.sleep(0.1)


class TestNextLayerCapture:
    """Per-layer capture of a Next frame (design-layer-capture.md): the routes, the layer words, PNG with alpha, the 400 / 409 answers and the
    switch. The pixels are asserted by the core tests (core/tests/emulator/video/next/nextlayercapture_test.cpp and nextlayerimage_test.cpp)."""

    def test_the_video_report_carries_the_record(self, api_client, next_emulator):
        record = get(api_client, next_emulator, "/state/next/video").json()["layer_capture"]
        assert record["enabled"] is True, "on by default"
        assert set(record) >= {"enabled", "frames", "last_frame", "bytes"}

    def test_a_layer_is_a_png_with_alpha(self, api_client, next_emulator):
        for layer in ("ula", "layer2", "tilemap", "sprites", "ids"):
            response = wait_for_layer_frame(api_client, next_emulator, layer)
            assert response.status_code == 200, (layer, response.text)
            body = response.json()
            assert body["layer"] == layer and body["format"] == "png" and body["alpha"] is True
            assert (body["width"], body["height"]) == (640, 512)
            data = base64.b64decode(body["data"])
            width, height, depth, colour = png_header(data)
            assert (width, height) == (640, 512), "the geometry of capture/screen"
            # the encoder picks the smallest PNG that holds the pixels: RGBA (colour type 6) or a palette (3) whose tRNS chunk carries the alpha
            assert colour in (0, 3, 6), (layer, colour)  # grey for a single-colour image
            if layer in ("layer2", "tilemap", "sprites"):  # nothing paints on a bare machine: the alpha is in the file
                assert colour == 6 or b"tRNS" in data, (layer, colour)
            assert body["frame"]["sequence"] >= 1
        assert "legend" in body and body["legend"][0]["name"] == "fallback", "the ids image carries its legend"

    def test_raw_bytes_and_flattened_alpha(self, api_client, next_emulator):
        assert wait_for_layer_frame(api_client, next_emulator).status_code == 200
        raw = get(api_client, next_emulator, "/capture/screen?layer=tilemap&format=rgba").json()
        assert raw["format"] == "rgba"
        assert len(base64.b64decode(raw["data"])) == 640 * 512 * 4
        flat = get(api_client, next_emulator, "/capture/screen?layer=tilemap&format=rgba&alpha=false").json()
        pixels = base64.b64decode(flat["data"])
        assert all(pixels[i] == 255 for i in range(3, len(pixels), 4 * 997)), "alpha=false: every pixel opaque"

    def test_smaller_sizes(self, api_client, next_emulator):
        assert wait_for_layer_frame(api_client, next_emulator).status_code == 200
        for size, (w, h) in (("grid", (640, 256)), ("pixel", (320, 256)), ("screen", (640, 512))):
            body = get(api_client, next_emulator, f"/capture/screen?layer=ula&format=rgba&size={size}").json()
            assert (body["width"], body["height"], body["scale"]) == (w, h, size)
            assert len(base64.b64decode(body["data"])) == w * h * 4
        assert get(api_client, next_emulator, "/capture/screen?layer=ula&size=raw").status_code == 400
        every = get(api_client, next_emulator, "/capture/next-layers?format=rgba&size=pixel").json()
        assert (every["width"], every["height"]) == (320, 256)
        assert len(base64.b64decode(every["layers"]["sprites"]["data"])) == 320 * 256 * 4

    def test_all_layers_of_one_frame(self, api_client, next_emulator):
        assert wait_for_layer_frame(api_client, next_emulator).status_code == 200
        response = get(api_client, next_emulator, "/capture/next-layers")
        assert response.status_code == 200, response.text
        body = response.json()
        assert set(body["layers"]) == {"ula", "layer2", "tilemap", "sprites", "ids"}
        assert len(body["legend"]) == 8
        assert png_header(base64.b64decode(body["layers"]["ula"]["data"]))[:2] == (640, 512)
        assert get(api_client, next_emulator, "/capture/next-layers?format=gif").status_code == 400

    def test_bad_words_are_400(self, api_client, next_emulator):
        for query in ("layer=layer3", "layer=ula&format=gif", "layer=ula&alpha=maybe", "layer=ula&area=screen"):
            assert get(api_client, next_emulator, f"/capture/screen?{query}").status_code == 400, query

    def test_composed_is_the_ordinary_screenshot(self, api_client, next_emulator):
        response = get(api_client, next_emulator, "/capture/screen?layer=composed")
        assert response.status_code in (200, 409)  # 409 = no presented frame yet, the ordinary answer
        if response.status_code == 200:
            assert "layer" not in response.json()

    def test_the_switch(self, api_client, next_emulator):
        assert wait_for_layer_frame(api_client, next_emulator).status_code == 200
        off = post(api_client, next_emulator, "/next/layer-capture", {"enabled": False})
        assert off.status_code == 200 and off.json()["enabled"] is False
        refused = get(api_client, next_emulator, "/capture/screen?layer=ula")
        assert refused.status_code == 409 and refused.json()["kind"] == "layer-capture-off", refused.text
        assert get(api_client, next_emulator, "/capture/next-layers").status_code == 409
        on = post(api_client, next_emulator, "/next/layer-capture", {"enabled": True})
        assert on.status_code == 200 and on.json()["enabled"] is True
        assert wait_for_layer_frame(api_client, next_emulator).status_code == 200, "a frame after the switch"
        report = post(api_client, next_emulator, "/next/layer-capture", {})
        assert report.status_code == 200 and report.json()["frames"] >= 1
        assert post(api_client, next_emulator, "/next/layer-capture", {"enabled": "yes"}).status_code == 400

    def test_the_pixel_inspector(self, api_client, next_emulator):
        assert wait_for_layer_frame(api_client, next_emulator).status_code == 200
        response = get(api_client, next_emulator, "/state/next/pixel?x=100&y=100&sx=1")
        assert response.status_code == 200, response.text
        body = response.json()
        assert body["ok"] is True and body["pixel"]["x"] == 100 and body["pixel"]["y"] == 100 and body["pixel"]["subpixel"] == 1
        assert body["winner"]["layer"] in ("ula", "border", "layer2", "tilemap", "sprite", "blend", "stencil", "fallback")
        assert set(body["layers"]) == {"ula", "layer2", "tilemap", "sprites"}
        for layer in body["layers"].values():
            assert set(layer) >= {"opaque", "rgb9", "wins", "why", "source"}
        assert body["layers"]["ula"]["source"]["kind"] in ("paper", "paper_hicolour", "paper_hires", "lores", "border", "disabled")
        assert body["colour"]["rgb"].startswith("#") and body["layer_order"]
        # the same pixel as the layer image says: the winner of the ids image is the winner here
        for query in ("x=320&y=0", "x=0&y=256", "x=-1&y=0", "x=1", "y=1", "x=1&y=1&sx=2", "x=zz&y=1"):
            assert get(api_client, next_emulator, f"/state/next/pixel?{query}").status_code == 400, query
        assert post(api_client, next_emulator, "/next/layer-capture", {"enabled": False}).status_code == 200
        refused = get(api_client, next_emulator, "/state/next/pixel?x=1&y=1")
        assert refused.status_code == 409 and refused.json()["error"] == "Conflict", refused.text
        assert post(api_client, next_emulator, "/next/layer-capture", {"enabled": True}).status_code == 200

    def test_another_machine_has_no_layers(self, api_client):
        response = api_client.create_and_start_emulator(model="48K")
        emulator_id = response["id"]
        try:
            refused = get(api_client, emulator_id, "/capture/screen?layer=ula")
            assert refused.status_code == 409 and refused.json()["kind"] == "not-next", refused.text
            assert get(api_client, emulator_id, "/capture/next-layers").status_code == 409
            assert post(api_client, emulator_id, "/next/layer-capture", {"enabled": True}).status_code == 409
            assert get(api_client, emulator_id, "/state/next/pixel?x=1&y=1").status_code == 409
            assert get(api_client, emulator_id, "/capture/screen?layer=layer3").status_code == 400
        finally:
            api_client.stop_emulator(emulator_id)
            api_client.delete_emulator(emulator_id)


class TestNextInterpreters:
    """The report bindings of the Python and Lua planes on a live NEXT. They are verification tests, not core tests: the core library
    does not link either interpreter, and the plane is only real inside unreal-qt / unreal-cli."""

    def test_python_bindings(self, api_client, next_emulator):
        if not api_client.get_python_status().get("available", False):
            pytest.skip("Python interpreter not available")
        code = f"""
import unreal_emulator as ue
emu = ue.emu_get('{next_emulator}')
assert emu is not None, 'emulator not found'

d = emu.next_dma()
assert d['mode'] in ('zxn', 'z80') and 'a' in d and 'b' in d and 'status' in d, d
v = emu.next_video()
for key in ('layer_order', 'ula', 'layer2', 'tilemap', 'raster', 'timing'):
    assert key in v, key
p = emu.next_palette(palette='ula_1', range='0-3')
assert len(p['palettes'][0]['entries']) == 4, p
assert 'device' in emu.next_ports(0x6B, 'w')['describe']
r = emu.next_nextreg(0x07)
assert r['reg'] == '0x07', r
assert 'instructions' in emu.next_copper(count=2)
assert 'flags' in emu.next_sprites(count=2)

w = emu.next_nextreg_write(0x4C, 5, 'nextreg')
assert w['after'] == '0x05' and w['door'] == 'nextreg', w
assert emu.next_nextreg(0x4C)['value'] == '0x05'
try:
    emu.next_nextreg_write(0x4C, 5, 'nonsense')
except ValueError:
    pass
else:
    raise AssertionError('a bad door must raise ValueError')

# per-layer capture (design-layer-capture.md)
state = emu.next_layer_capture_control()
assert state['enabled'] is True and 'frames' in state and 'bytes' in state, state
shot = emu.capture_screen_layer('tilemap')
if not shot['success']:
    assert shot['kind'] == 'no-frame', shot   # a frame has not completed yet: the only allowed refusal
else:
    assert shot['layer'] == 'tilemap' and shot['width'] == 640 and shot['height'] == 512 and shot['alpha'] is True, shot
    assert shot['frame']['sequence'] >= 1 and len(shot['data']) > 100, shot
    ids = emu.capture_screen_layer('ids', 'rgba')
    assert ids['success'] and len(ids['legend']) == 8 and ids['format'] == 'rgba', ids
bad = emu.capture_screen_layer('layer3')
assert bad['success'] is False and bad['kind'] == 'bad-parameter', bad
off = emu.next_layer_capture_control(False)
assert off['enabled'] is False, off
assert emu.capture_screen_layer('ula')['kind'] == 'layer-capture-off'
try:
    emu.next_pixel(1, 1)
except RuntimeError:
    pass
else:
    raise AssertionError('next_pixel with the capture off must raise RuntimeError')
assert emu.next_layer_capture_control(True)['enabled'] is True
try:
    emu.next_pixel(320, 0)
except ValueError:
    pass
else:
    raise AssertionError('x = 320 must raise ValueError')
print('NEXT-PY-OK')
"""
        resp = api_client.exec_python(code)
        assert resp["success"] is True, resp
        assert "NEXT-PY-OK" in resp["output"], resp

    def test_lua_bindings(self, api_client, next_emulator):
        if not api_client.get_lua_status().get("available", False):
            pytest.skip("Lua interpreter not available")
        # select the NEXT instance: the Lua globals act on the selected emulator
        api_client.session.post(api_client._url(f"/api/v1/emulator/{next_emulator}/select"))
        code = """
local d = assert(next_dma())
assert(d.mode == "zxn" or d.mode == "z80", "dma mode")
local v = assert(next_video())
assert(v.tilemap ~= nil and v.raster ~= nil and v.layer_order ~= nil, "video")
local p = assert(next_palette({palette = "ula_1", range = "0-3"}))
assert(#p.palettes[1].entries == 4, "palette entries")
assert(next_ports(0x6B, "w").describe.device ~= nil, "ports")
assert(next_nextreg(0x07).reg == "0x07", "nextreg")
assert(next_copper({count = 2}).instructions ~= nil, "copper")
assert(next_sprites({count = 2}).flags ~= nil, "sprites")
local w = assert(next_nextreg_write(0x4C, 5, "nextreg"))
assert(w.after == "0x05", "write")
local ok, err = next_nextreg_write(0x4C, 5, "nonsense")
assert(ok == nil and err ~= nil, "bad door must be nil + error")

-- per-layer capture (design-layer-capture.md)
local state = assert(next_layer_capture_control())
assert(state.enabled == true and state.bytes ~= nil, "layer capture state")
local shot, why = capture_screen_layer("tilemap")
if shot == nil then
    assert(why:find("no frame") ~= nil, "the only allowed refusal is: no frame yet, got " .. tostring(why))
else
    assert(shot.layer == "tilemap" and shot.width == 640 and shot.height == 512 and shot.alpha == true, "layer image")
    assert(shot.frame.sequence >= 1 and #shot.data > 100, "layer data")
    local ids = assert(capture_screen_layer("ids", {format = "rgba"}))
    assert(#ids.legend == 8 and #ids.data == 640 * 512 * 4, "ids image")
end
local bad, badWhy = capture_screen_layer("layer3")
assert(bad == nil and badWhy:find("Unknown layer") ~= nil, "bad layer")
assert(next_layer_capture_control({enabled = false}).enabled == false, "switch off")
local refused, refusedWhy = capture_screen_layer("ula")
assert(refused == nil and refusedWhy:find("Layer capture is off") ~= nil, "off refusal")
local pixel = next_pixel(100, 100)
assert(pixel == nil or (pixel.layers.ula.source.kind ~= nil and pixel.winner.layer ~= nil), "pixel")
local badPixel, badPixelWhy = next_pixel(320, 0)
assert(badPixel == nil and badPixelWhy:find("x must be 0%-319") ~= nil, "bad pixel")
local offPixel, offWhy = next_pixel(1, 1)
assert(offPixel == nil and offWhy:find("Layer capture is off") ~= nil, "off pixel")
assert(next_layer_capture_control({enabled = true}).enabled == true, "switch on")
print("NEXT-LUA-OK")
"""
        resp = api_client.exec_lua(code)
        assert resp["success"] is True, resp
        assert "NEXT-LUA-OK" in resp["output"], resp
