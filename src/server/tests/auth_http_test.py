#!/usr/bin/env python3
"""Real server + MySQL; isolated data, restart test, optional Qt Network smoke test."""
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
ROOT = Path(__file__).resolve().parents[3]
settings = ROOT / 'build/local-auth'
test = json.loads((settings / 'test.json').read_text())
runtime = json.loads((settings / 'http-test.json').read_text())
assert test['STORAGE_DB_NAME'] == runtime['STORAGE_DB_NAME'] == 'asynlog_storage_test'
account, password = 'http_test_' + secrets.token_hex(8), secrets.token_hex(24)
fixture_env = {**os.environ, **test, 'STORAGE_TEST_ACCOUNT': account, 'STORAGE_TEST_PASSWORD': password}
fixture = ROOT / 'build/mysql/mysql_http_fixture'
def fixture_run(action, token=''):
    subprocess.run([str(fixture), action], env={**fixture_env, 'STORAGE_TEST_TOKEN': token}, check=True)
def check(ok, name):
    if not ok:
        raise AssertionError(name)
    print('PASS ' + name, flush=True)
fixture_run('seed')
process = None
try:
    with tempfile.TemporaryDirectory(prefix='auth-http-', dir=ROOT / 'build') as directory:
        temp = Path(directory)
        work = temp / 'src/server'
        work.mkdir(parents=True)
        logs = temp / 'log_system/logs_code'
        logs.mkdir(parents=True)
        shutil.copy(ROOT / 'log_system/logs_code/config.conf', logs / 'config.conf')
        config = json.loads((ROOT / 'src/server/Storage.conf').read_text())
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        config['server_port'] = port
        (work / 'Storage.conf').write_text(json.dumps(config))
        url = f'http://127.0.0.1:{port}'
        def request(path, data=None, token=None, method=None):
            headers = {'Content-Type': 'application/json'}
            if token is not None:
                headers['Authorization'] = 'Bearer ' + token
            body = None if data is None else json.dumps(data).encode()
            req = urllib.request.Request(url + path, data=body, headers=headers, method=method)
            try:
                with urllib.request.urlopen(req, timeout=10) as response:
                    return response.status, json.load(response)
            except urllib.error.HTTPError as error:
                return error.code, json.load(error)
        def start():
            global process
            output = open(temp / 'server-output.log', 'ab')
            process = subprocess.Popen([str(ROOT / 'src/server/test')], cwd=work,
                                       env={**os.environ, **runtime}, stdout=output, stderr=output)
            output.close()
            for _ in range(100):
                if process.poll() is not None:
                    raise RuntimeError('Server failed; inspect ' + str(temp / 'server-output.log'))
                try:
                    if request('/health')[0] == 200:
                        return
                except (OSError, urllib.error.URLError):
                    pass
                time.sleep(.05)
            raise RuntimeError('Server startup timeout')
        def stop():
            global process
            if process:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
                process = None
        start()
        status, login = request('/api/v1/auth/login', {'account': account, 'password': password})
        check(status == 200 and len(login.get('token', '')) == 64 and bool(login.get('expiresAt')) and login.get('user', {}).get('username') == account, 'HTTP correct credentials')
        token = login['token']
        check(request('/api/v1/auth/login', {'account': account, 'password': 'wrong'})[0] == 401, 'HTTP wrong password')
        check(request('/api/v1/auth/login', {'account': 'missing_' + account, 'password': password})[0] == 401, 'HTTP missing user')
        check(request('/api/v1/auth/me', token=token)[0] == 200, 'HTTP protected request')
        with ThreadPoolExecutor(max_workers=6) as workers:
            statuses = list(workers.map(lambda _: request('/api/v1/auth/me', token=token)[0], range(6)))
        check(statuses == [200] * 6, 'six concurrent authenticated requests')
        check(request('/api/v1/auth/me')[0] == 401, 'HTTP no token')
        check(request('/api/v1/auth/me', token='0' * 64)[0] == 401, 'HTTP forged token')
        check(request('/')[0] == 401 and request('/upload', {}, method='POST')[0] == 401, 'legacy data routes require authentication')
        check(request('/api/v1/auth/login')[0] == 405, 'HTTP method validation')
        check(request('/api/v1/auth/login', {'account': 123, 'password': []})[0] == 400, 'HTTP JSON type validation')
        stop(); start()
        check(request('/api/v1/auth/me', token=token)[0] == 200, 'persistent token survives actual server process restart')
        check(request('/api/v1/auth/logout', {}, token=token)[0] == 200, 'HTTP logout')
        check(request('/api/v1/auth/me', token=token)[0] == 401, 'HTTP revoked token rejected')
        _, login = request('/api/v1/auth/login', {'account': account, 'password': password})
        fixture_run('expire', login['token'])
        check(request('/api/v1/auth/me', token=login['token'])[0] == 401, 'HTTP expired token rejected')
        qt = ROOT / 'build/client/client_auth_smoke'
        if qt.exists():
            subprocess.run([str(qt), url], env=fixture_env, check=True, timeout=60)
        else:
            print('SKIP Qt Network smoke: build/client/client_auth_smoke missing', flush=True)
        ui = ROOT / 'build/client/client_ui_smoke'
        if ui.exists():
            subprocess.run([str(ui), url], env={**fixture_env, 'QT_QPA_PLATFORM': 'offscreen'}, check=True, timeout=60)
        # Tokens/passwords must not appear in the server's file or console logs.
        stop()
        forbidden = [password.encode(), token.encode(), login['token'].encode(), runtime['STORAGE_DB_PASSWORD'].encode()]
        for log in [temp / 'server-output.log', *work.glob('logfile/*')]:
            if log.is_file():
                check(not any(secret in log.read_bytes() for secret in forbidden), 'server logs contain no credentials: ' + log.name)
finally:
    if process:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill(); process.wait()
    fixture_run('cleanup')
