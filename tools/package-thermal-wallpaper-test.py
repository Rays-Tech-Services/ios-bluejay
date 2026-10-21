#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'package_thermal_wallpaper', Path(__file__).with_name('package-thermal-wallpaper.py'))
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class FlagsTest(unittest.TestCase):
    def test_enable_and_replace(self):
        line = package.command_line('rd=ufsroot0 cpus=8 sweetbattery=0 sweetthermal=0 sweetfix_posterboard=0 sweetthermal=0')
        words = line.split()
        self.assertIn('cpus=8', words)
        for flag in ('sweetbattery=2', 'sweetthermal=1', 'sweetfix_posterboard=1'):
            self.assertEqual(words.count(flag), 1)
        self.assertNotIn('sweetthermal=0', words)
        self.assertEqual(package.command_line(line), line)

    def test_ramdisk_rejected(self):
        with self.assertRaises(ValueError):
            package.command_line('rd=md0')

    def test_no_silent_truncation(self):
        with self.assertRaises(ValueError):
            package.command_line('x' * 600)


if __name__ == '__main__':
    unittest.main()
