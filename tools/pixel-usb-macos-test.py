#!/usr/bin/env python3
"""Ensure local-link setup never selects an unrelated Mac network interface."""
import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location('pixelusb', pathlib.Path(__file__).with_name('pixel-usb-macos.py'))
pixelusb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pixelusb)


def entry(name, mac):
    return f'{name}: flags=8863<UP,BROADCAST> mtu 1500\n\tether {mac}\n\tstatus: active\n'


class InterfaceSelection(unittest.TestCase):
    def test_other_interfaces_are_ignored(self):
        self.assertIsNone(pixelusb.find_pixel(entry('en0', '00:11:22:33:44:55')))

    def test_match_is_independent_of_interface_order(self):
        data = entry('en0', '00:11:22:33:44:55') + entry('en12', '02:00:5E:77:EE:01')
        self.assertEqual(pixelusb.find_pixel(data), 'en12')

    def test_phone_mac_is_not_host_mac(self):
        self.assertIsNone(pixelusb.find_pixel(entry('en5', '02:00:5e:77:ee:02')))

    def test_ambiguous_interfaces_are_rejected(self):
        with self.assertRaises(ValueError):
            pixelusb.find_pixel(entry('en5', pixelusb.PIXEL_MAC) + entry('en6', pixelusb.PIXEL_MAC))


if __name__ == '__main__':
    unittest.main()
