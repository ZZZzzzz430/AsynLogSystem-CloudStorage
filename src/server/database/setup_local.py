#!/usr/bin/env python3
"""Run locally after building. No credentials on argv or in Git; never touches other databases."""
import getpass
import json
import os
from pathlib import Path
import secrets
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/local-auth'
OUT.mkdir(parents=True, exist_ok=True, mode=0o700)
os.chmod(OUT, 0o700)

def save(name, data):
    path = OUT / name
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w') as f:
        json.dump(data, f, indent=2)

def config(database, user, password):
    return dict(STORAGE_DB_HOST='127.0.0.1', STORAGE_DB_PORT='3306',
                STORAGE_DB_NAME=database, STORAGE_DB_USER=user, STORAGE_DB_PASSWORD=password,
                STORAGE_DB_REQUIRE_VERIFIED_TLS='0')

if (OUT / 'runtime.json').exists():
    raise SystemExit('Local configuration already exists; refusing to overwrite credentials. See README.')
admin = input('MySQL administrator username [root]: ').strip() or 'root'
password = getpass.getpass('MySQL administrator password (not saved): ')
# Escape option-file syntax, not SQL. Admin credentials only live in a 0600 temporary file.
def option(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\r', '\\r') + '"'
account = input('Initial cloud username [demo]: ').strip() or 'demo'
cloud_password = getpass.getpass('Initial cloud password (at least 12 bytes, not saved): ')
if cloud_password != getpass.getpass('Repeat cloud password: '):
    raise SystemExit('Passwords differ.')
if not account or len(account.encode()) > 64 or not 12 <= len(cloud_password.encode()) <= 1024 or "\n" in cloud_password:
    raise SystemExit("Initial username/password length invalid")
with tempfile.TemporaryDirectory(prefix='asynlog-setup-') as directory:
    defaults = Path(directory) / 'mysql.cnf'
    defaults.write_text('[client]\nhost=127.0.0.1\nport=3306\nprotocol=TCP\nuser=' + option(admin) + '\npassword=' + option(password) + '\n')
    defaults.chmod(0o600)
    def mysql(sql):
        subprocess.run(['mysql', '--defaults-extra-file=' + str(defaults), '--batch'],
                       input=sql, text=True, check=True, stdout=subprocess.DEVNULL)
    migration = (Path(__file__).parent / 'migrations/001_auth.sql').read_text()
    mysql(migration)
    mysql(migration.replace('asynlog_storage', 'asynlog_storage_test'))
    suffix = secrets.token_hex(4)
    runtime = config('asynlog_storage', 'storage_app_' + suffix, secrets.token_hex(24))
    test = config('asynlog_storage_test', 'storage_test_' + suffix, secrets.token_hex(24))
    http = config('asynlog_storage_test', 'storage_http_' + suffix, secrets.token_hex(24))
    for conf, provisioning in [(runtime, False), (test, True), (http, False)]:
        user, secret, database = conf['STORAGE_DB_USER'], conf['STORAGE_DB_PASSWORD'], conf['STORAGE_DB_NAME']
        # All interpolated values above are fixed identifiers or generated hex, never external SQL.
        mysql(f"CREATE USER '{user}'@'127.0.0.1' IDENTIFIED BY '{secret}';\n"
              + (f"GRANT SELECT,INSERT,UPDATE,DELETE ON {database}.* TO '{user}'@'127.0.0.1';" if provisioning else
                 f"GRANT SELECT ON {database}.users TO '{user}'@'127.0.0.1';\n"
                 f"GRANT SELECT,INSERT,DELETE ON {database}.sessions TO '{user}'@'127.0.0.1';"))
    # Administrator is used only by this one-off provisioning process, never by the HTTP server.
    subprocess.run([str(ROOT / 'build/mysql/storage_create_user'), account],
                   input=cloud_password + '\n', text=True, check=True,
                   env={**os.environ, **config('asynlog_storage', admin, password)})
    save('runtime.json', runtime)
    save('test.json', test)
    save('http-test.json', http)
print('Independent databases and dedicated accounts ready. Config: build/local-auth/ (0600).')
print('Run: python3 src/server/database/run_local.py runtime build/mysql/mysql_connection_probe')
