"""A small WebAPI client for driving an unreal-ng instance from the unreal-asm checks.

Run your own instance on its own ports (other sessions may run theirs on the defaults), e.g.
    UNREAL_WEBAPI_PORT=8095 UNREAL_CLI_PORT=8195 UNREAL_MCP_PORT=8295 UNREAL_GDB_PORT=8395 \
    UNREAL_DEZOG_PORT=8495 UNREAL_ZRCP_PORT=8595 <build>/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
and point the tools at it with --url http://localhost:8095 (or UNREAL_ASM_EMULATOR_URL).
"""
import json
import os
import time
import urllib.error
import urllib.request


class Emulator:
    def __init__(self, url=None, emulator_id=None, model='PENTAGON'):
        self.url = (url or os.environ.get('UNREAL_ASM_EMULATOR_URL') or 'http://localhost:8095').rstrip('/') + '/api/v1/emulator'
        if not emulator_id:
            emulator_id = self._request('POST', self.url + '/start', {'model': model})['id']
        self.id = emulator_id
        self.base = f'{self.url}/{self.id}'

    @staticmethod
    def _request(method, url, body=None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json'}, method=method)
        try:
            return json.load(urllib.request.urlopen(request))
        except urllib.error.HTTPError as e:
            return json.load(e)

    def post(self, path, body=None):
        return self._request('POST', self.base + path, body or {})

    def get(self, path):
        return self._request('GET', self.base + path)

    def idle(self):
        """Waits until a queued key sequence has been typed"""
        for _ in range(600):
            if not self.get('/keyboard/status').get('sequence_running'):
                return
            time.sleep(0.1)

    def tap(self, key, frames=4):
        self.post('/keyboard/tap', {'key': key, 'frames': frames})
        self.idle()
        time.sleep(0.3)

    def type(self, text):
        self.post('/keyboard/type', {'text': text, 'delay_frames': 6})
        self.idle()
        time.sleep(0.5)

    def screenshot(self, path):
        urllib.request.urlopen(f'{self.base}/capture/screen?area=full&format=png&path={os.path.abspath(path)}').read()

    def insert_disk(self, image):
        """Drive A gets the image; TTD recording (rolling history) starts afresh, as media swaps wipe it"""
        self.post('/ttd/stop')
        result = self.post('/media/A/swap', {'path': os.path.abspath(image), 'discard': True, 'immediate': True})
        self.post('/ttd/start')
        self.post('/ttd/history-limit', {'frames': 15000})
        return result.get('ok', False)

    def run_trdos(self, program, wait=10):
        """Reset, choose TR-DOS in the Pentagon menu, RUN "program" """
        self.post('/reset')
        time.sleep(3)
        for _ in range(4):
            self.tap('down')
        self.tap('enter')
        time.sleep(3)
        outcome = self.post('/basic/run', {'command': f'RUN "{program}"'}).get('outcome')
        time.sleep(wait)
        return outcome

    def read(self, address, length):
        """Bytes of the Z80 address space as the machine sees it now"""
        dump = self.get(f'/memory/read/{address}?length={length}')['hexdump']
        out = bytearray()
        for line in dump.split('\n'):
            if line:
                out += bytes(int(x, 16) for x in line.split(':', 1)[1].split('|')[0].split())
        return bytes(out[:length])

    def write(self, address, data):
        return self.post('/memory/write', {'address': address, 'data': list(data)})

    def stop_recording(self):
        self.post('/ttd/stop')
