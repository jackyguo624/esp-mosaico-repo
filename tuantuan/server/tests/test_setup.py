"""Local setup preserves user secrets and generates private configuration."""
from pathlib import Path
import os
import stat
import tempfile
import unittest
from setup import prepare_config


class SetupTests(unittest.TestCase):
    def test_new_config_has_random_private_token(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'config.example.env').write_text(
                'DEVICE_TOKEN=replace-with-a-long-random-token\n'
                'DEEPSEEK_API_KEY=\nTIMEZONE=Asia/Shanghai\n')
            path, created = prepare_config(root)
            self.assertTrue(created)
            values = dict(line.split('=', 1) for line in path.read_text().splitlines())
            self.assertGreaterEqual(len(values['DEVICE_TOKEN']), 32)
            self.assertNotEqual(values['DEVICE_TOKEN'], 'replace-with-a-long-random-token')
            self.assertEqual(values['DEEPSEEK_API_KEY'], '')
            self.assertEqual(values['TIMEZONE'], 'Asia/Singapore')
            if os.name == 'posix':
                self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)

    def test_existing_config_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'config.env'
            content = 'DEVICE_TOKEN=existing-local-token\nDEEPSEEK_API_KEY=example\n'
            path.write_text(content)
            # No template is needed when configuration already exists.
            result, created = prepare_config(root)
            self.assertEqual(result, path)
            self.assertFalse(created)
            self.assertEqual(path.read_text(), content)


if __name__ == '__main__':
    unittest.main()
