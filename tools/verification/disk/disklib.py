"""Shared WebAPI helpers for the disk verification scripts: launch an offscreen unreal-qt on ports of its own, start a
muted machine, insert a disk image, read registers and the floppy controller state."""
import json
import os
import socket
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
DEFAULT_BINARY = os.path.join(REPO, 'cmake-build-agent-release', 'bin', 'unreal-qt.app', 'Contents', 'MacOS', 'unreal-qt')

# Model per image extension when --model auto: +3DOS / CPC style DSK on a +3, the rest on a Pentagon (TR-DOS)
MODEL_BY_EXT = {'.dsk': 'PLUS3', '.trd': 'PENTAGON', '.scl': 'PENTAGON', '.fdi': 'PENTAGON', '.udi': 'PENTAGON',
                '.td0': 'PENTAGON'}


def model_for(path, model):
    if model != 'auto':
        return model
    return MODEL_BY_EXT.get(os.path.splitext(path)[1].lower(), 'PENTAGON')


class Api:
    """WebAPI client of one instance (http://127.0.0.1:<port>/api/v1)"""

    def __init__(self, port):
        self.port = port

    def call(self, method, path, body=None, timeout=120):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(f'http://127.0.0.1:{self.port}/api/v1{path}', data=data, method=method,
                                         headers={'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                raw, code = response.read(), response.status
        except urllib.error.HTTPError as error:
            raw, code = error.read(), error.code
        try:
            return code, json.loads(raw)
        except ValueError:
            return code, raw

    def wait_ready(self, process=None, seconds=60):
        for _ in range(int(seconds * 2)):
            try:
                if self.call('GET', '/emulator', timeout=2)[0] == 200:
                    return
            except OSError:
                pass
            if process is not None and process.poll() is not None:
                raise SystemExit(f'the emulator exited with {process.returncode}')
            time.sleep(0.5)
        raise SystemExit(f'no WebAPI answer on port {self.port}')


class Machine:
    """One emulator instance started for an image; destroyed by close()"""

    def __init__(self, api, model):
        self.api = api
        code, reply = api.call('POST', '/emulator/start', {'model': model})
        if code >= 300 or not isinstance(reply, dict) or 'id' not in reply:
            raise RuntimeError(f'cannot start {model}: {reply}')
        self.base = f"/emulator/{reply['id']}"
        # Key clicks and loader noise stay silent
        api.call('PUT', self.base + '/audio/mixer/master', {'muted': True})

    def call(self, method, path, body=None, timeout=120):
        return self.api.call(method, self.base + path, body, timeout)

    def insert(self, path, drive='A'):
        """Returns (ok, message)"""
        code, reply = self.call('POST', f'/disk/{drive}/insert', {'path': os.path.abspath(path)})
        message = reply.get('message', '') if isinstance(reply, dict) else str(reply)
        return code < 300, message

    def resume(self):
        self.call('POST', '/resume', {})

    def pause(self):
        self.call('POST', '/pause', {})

    def run_frames(self, count, chunk=1000):
        while count > 0:
            step = min(chunk, count)
            self.call('POST', '/run_frames', {'count': step})
            count -= step

    def tap(self, key):
        self.call('POST', '/keyboard/tap', {'key': key})

    def screenshot(self, png):
        code, _ = self.call('GET', '/capture/screen?format=png&path=' + urllib.parse.quote(os.path.abspath(png)))
        return code == 200 and os.path.exists(png)

    def registers(self):
        return self.call('GET', '/registers')[1]

    def fdc(self):
        code, reply = self.call('GET', '/state/fdc')
        return reply if code == 200 and isinstance(reply, dict) else {}

    def memory(self, address, length):
        return self.call('GET', f'/memory/{address:#06x}?len={length}')[1]

    def disasm(self, address, count):
        reply = self.call('GET', f'/disasm?address={address:#06x}&count={count}')[1]
        return reply.get('instructions', []) if isinstance(reply, dict) else []

    def close(self):
        self.api.call('DELETE', self.base)


def launch(binary, port):
    """unreal-qt offscreen with its WebAPI on port and every other server on ports next to it; returns the process"""
    with socket.socket() as probe:
        if probe.connect_ex(('127.0.0.1', port)) == 0:
            raise SystemExit(f'port {port} is taken by another process: pick another with --port')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    for name, offset in (('UNREAL_WEBAPI_PORT', 0), ('UNREAL_CLI_PORT', 100), ('UNREAL_MCP_PORT', 200),
                         ('UNREAL_GDB_PORT', 300), ('UNREAL_DEZOG_PORT', 400), ('UNREAL_ZRCP_PORT', 500)):
        env[name] = str(port + offset)
    # The user's View settings, Stream Deck daemon and Recent Files stay out
    env.setdefault('UNREAL_QT_VIEW_SETTINGS', 'off')
    env.setdefault('UNREAL_STREAMDECK_SUPERVISOR', 'off')
    env.setdefault('UNREAL_RECENT_FILES', 'off')
    process = subprocess.Popen([binary], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    Api(port).wait_ready(process)
    return process


def stop(process):
    if process is None:
        return
    process.terminate()
    try:
        process.wait(20)
    except subprocess.TimeoutExpired:
        process.kill()


def add_instance_args(parser):
    parser.add_argument('--launch', action='store_true', help='start unreal-qt offscreen (--binary)')
    parser.add_argument('--binary', default=DEFAULT_BINARY,
                        help='unreal-qt to start with --launch (default: the agent release build)')
    parser.add_argument('--port', type=int, default=8590,
                        help='WebAPI port: of the launched instance (others at +100..+500), or of a running one')


def open_instance(args):
    """Returns (api, process or None)"""
    process = launch(args.binary, args.port) if args.launch else None
    api = Api(args.port)
    if process is None:
        api.wait_ready(seconds=5)
    return api, process
