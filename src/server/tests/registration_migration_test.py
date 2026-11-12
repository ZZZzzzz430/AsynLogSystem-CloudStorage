#!/usr/bin/env python3
"""Offline checks with synthetic configuration; never connects to MySQL."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[1] / 'database/upgrade_registration.py'
spec = importlib.util.spec_from_file_location('upgrade_registration', MODULE)
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.configs = {}
        for target, (filename, database, prefix) in migration.TARGETS.items():
            config = dict(STORAGE_DB_NAME=database, STORAGE_DB_USER=prefix + '1234abcd',
                          STORAGE_DB_HOST='127.0.0.1', STORAGE_DB_PORT='3306',
                          STORAGE_DB_PASSWORD='synthetic-secret-must-not-be-printed')
            self.configs[target] = config
            self.write(target)

    def write(self, target):
        filename = migration.TARGETS[target][0]
        (self.root / filename).write_text(json.dumps(self.configs[target]), encoding='utf-8')

    def test_plan_only_adds_insert_for_two_existing_accounts(self):
        sql, port = migration.build_plan(self.root, ['runtime', 'http-test'])
        self.assertEqual(port, '3306')
        self.assertEqual(sql.count('GRANT INSERT ON '), 2)
        self.assertIn("asynlog_storage.users TO 'storage_app_1234abcd'", sql)
        self.assertIn("asynlog_storage_test.users TO 'storage_http_1234abcd'", sql)
        self.assertNotIn('{{', sql)
        self.assertNotIn('synthetic-secret', sql)
        self.assertNotIn('GRANT ALL', sql)

    def test_preview_never_connects_or_prompts_for_password(self):
        output = io.StringIO()
        with patch('sys.argv', ['upgrade', '--config-dir', str(self.root)]), \
                patch.object(migration.subprocess, 'run') as run, \
                patch.object(migration.getpass, 'getpass') as prompt, \
                contextlib.redirect_stdout(output):
            self.assertEqual(migration.main(), 0)
        run.assert_not_called()
        prompt.assert_not_called()
        self.assertIn('PREVIEW ONLY', output.getvalue())
        self.assertNotIn('synthetic-secret', output.getvalue())

    def test_invalid_account_database_host_or_port_is_rejected(self):
        invalid = {
            'STORAGE_DB_USER': ["x'; GRANT ALL ON *.* TO 'x", 'root'],
            'STORAGE_DB_NAME': ['second_hand_platform'],
            'STORAGE_DB_HOST': ['remote.example'],
            'STORAGE_DB_PORT': ['0', '65536', '3306\npassword=bad'],
        }
        for field, values in invalid.items():
            original = self.configs['runtime'][field]
            for value in values:
                with self.subTest(field=field, value=value):
                    self.configs['runtime'][field] = value
                    self.write('runtime')
                    with self.assertRaises(ValueError):
                        migration.build_plan(self.root, ['runtime'])
            self.configs['runtime'][field] = original
            self.write('runtime')

    def test_missing_second_config_prevents_any_apply(self):
        (self.root / 'http-test.json').unlink()
        with patch('sys.argv', ['upgrade', '--config-dir', str(self.root), '--apply']), \
                patch.object(migration.subprocess, 'run') as run, \
                patch.object(migration.getpass, 'getpass') as prompt, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(migration.main(), 1)
        run.assert_not_called()
        prompt.assert_not_called()

    def test_different_ports_require_separate_migrations(self):
        self.configs['http-test']['STORAGE_DB_PORT'] = '3307'
        self.write('http-test')
        with self.assertRaises(ValueError):
            migration.build_plan(self.root, ['runtime', 'http-test'])
        self.assertEqual(migration.build_plan(self.root, ['http-test'])[1], '3307')


if __name__ == '__main__':
    unittest.main()
