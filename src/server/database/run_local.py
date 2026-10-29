#!/usr/bin/env python3
"""Load ignored local environment and exec without printing credentials."""
import json
import os
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[3]
if len(sys.argv) < 3 or sys.argv[1] not in ('runtime', 'test', 'http-test'):
    raise SystemExit('Usage: run_local.py runtime|test|http-test COMMAND [ARGS...]')
path = ROOT / 'build/local-auth' / (sys.argv[1] + '.json')
if path.stat().st_mode & 0o077:
    raise SystemExit('Credential file must have mode 0600')
settings = json.loads(path.read_text())
os.environ.update(settings)
os.execvp(sys.argv[2], sys.argv[2:])
