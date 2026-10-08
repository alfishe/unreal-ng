"""A small WebAPI client for driving an unreal-ng instance from the unreal-asm checks.

The WebAPI port is a parameter (default 8090, the emulator's own default). Run your own instance on ports of its own
when other sessions use the defaults, e.g.
    PORT=8095
    UNREAL_WEBAPI_PORT=$PORT UNREAL_CLI_PORT=$((PORT+100)) UNREAL_MCP_PORT=$((PORT+200)) UNREAL_GDB_PORT=$((PORT+300)) \
    UNREAL_DEZOG_PORT=$((PORT+400)) UNREAL_ZRCP_PORT=$((PORT+500)) <build>/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
and give the tools --port $PORT (a whole --url, or UNREAL_ASM_EMULATOR_URL, for another host).
"""
import json
import os
import time
import urllib.error
import urllib.request


DEFAULT_PORT = 8090   # the emulator's WebAPI default


class Emulator:
    def __init__(self, url=None, emulator_id=None, model='PENTAGON', ram_size=None, port=DEFAULT_PORT):
        url = url or os.environ.get('UNREAL_ASM_EMULATOR_URL') or f'http://localhost:{port}'
        self.url = url.rstrip('/') + '/api/v1/emulator'
        created = not emulator_id
        if created:
            body = {'model': model}
            if ram_size:
                body['ram_size'] = ram_size
            emulator_id = self._request('POST', self.url + '/start', body)['id']
        self.id = emulator_id
        self.base = f'{self.url}/{self.id}'
        if created:
            self.ensure_running()

    def ensure_running(self):
        """A second instance comes up paused: typed keys would wait for it forever"""
        # it can report running for a moment before the new instance settles paused: wait for two running reads
        settled = 0
        for _ in range(50):
            if self.get('').get('state') == 'running':
                settled += 1
                if settled == 2:
                    return
            else:
                settled = 0
                self.post('/resume')
            time.sleep(0.2)

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
        self.ensure_running()
        self.post('/ttd/stop')
        result = self.post('/media/A/swap', {'path': os.path.abspath(image), 'discard': True, 'immediate': True})
        self.post('/ttd/start')
        self.post('/ttd/history-limit', {'frames': 15000})
        return result.get('ok', False)

    def run_trdos(self, program, wait=10):
        """Reset, choose TR-DOS in the Pentagon menu, RUN "program" """
        self.ensure_running()
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

    def read_disk_file(self, name, type_):
        """(start, bytes) of the last catalog entry NAME.T on drive A, read sector by sector; None when missing"""
        import base64
        entries = [f for f in self.get('/disk/A/catalog').get('files', []) if f['name'].strip() == name and f['type'] == type_]
        if not entries:
            return None
        f = entries[-1]
        data = b''
        for k in range(f['sectors']):
            track, sector = divmod(f['first_track'] * 16 + f['first_sector'] + k, 16)
            data += base64.b64decode(self.get(f'/disk/A/sector/{track // 2}/{track % 2}/{sector + 1}')['data_base64'])
        return f['start'], data[:f['length']]

    def write(self, address, data):
        return self.post('/memory/write', {'address': address, 'data': list(data)})

    def stop_recording(self):
        self.post('/ttd/stop')
