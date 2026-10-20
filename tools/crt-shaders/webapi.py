"""A WebAPI client on one connection (a control session holds per connection) for the capture tools"""
import http.client
import json
import time


class WebAPI:
    def __init__(self, host, port, emulator):
        self.conn = http.client.HTTPConnection(host, port, timeout=120)
        self.headers = {'Content-Type': 'application/json'}
        self.emulator = emulator

    def call(self, method, path, body=None):
        """`path` below /api/v1/emulator/<id>; returns (status, JSON answer)"""
        self.conn.request(method, f'/api/v1/emulator/{self.emulator}{path}',
                          body=json.dumps(body) if body is not None else None, headers=self.headers)
        r = self.conn.getresponse()
        data = r.read()
        return r.status, json.loads(data or b'{}')

    def take_control(self):
        """A control session (the GUI may hold one for a while)"""
        for _ in range(90):
            status, answer = self.call('POST', '/control/session')
            if status == 200:
                self.headers['X-Unreal-Session'] = answer['session']
                return
            time.sleep(1)
        raise SystemExit(f'no control session: {status} {answer}')

    def release_control(self):
        self.call('DELETE', '/control/session')
        self.headers.pop('X-Unreal-Session', None)

    def screen(self, path):
        """The presented frame (352 x 288 with the border) into a PNG file"""
        status, answer = self.call('GET', f'/capture/screen?path={path}')
        assert status == 200, (status, answer)
