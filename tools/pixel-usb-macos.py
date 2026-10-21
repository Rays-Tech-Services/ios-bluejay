#!/usr/bin/env python3
"""Find the Pixel CDC-ECM interface on macOS; optionally bring up its local link.

Default is read-only. --configure-link changes only the interface with our exact
locally administered permanent host MAC. This establishes a local link, not internet sharing.
--listen prints the phone's UDP kernel log (192.168.7.2 -> host port 5140).
"""
import argparse
import json
import re
import socket
import subprocess
import sys

PIXEL_MAC = '02:00:5e:77:ee:01'


def find_pixel(text):
    matches = []
    for section in re.split(r'(?=^[A-Za-z][A-Za-z0-9]*: flags=)', text, flags=re.M):
        name = re.match(r'([A-Za-z][A-Za-z0-9]*): flags=', section)
        mac = re.search(r'^\s+ether\s+([0-9a-f:]{17})\s*$', section, re.M | re.I)
        if name and mac and mac[1].lower() == PIXEL_MAC:
            matches.append(name[1])
    # DriverKit can expose a temporary current MAC in ifconfig while the
    # permanent ECM descriptor MAC is correctly reported by networksetup.
    for section in re.split(r'(?=^Hardware Port: )', text, flags=re.M):
        name = re.search(r'^Device: ([A-Za-z][A-Za-z0-9]*)$', section, re.M)
        mac = re.search(r'^Ethernet Address: ([0-9a-f:]{17})$', section, re.M | re.I)
        if name and mac and mac[1].lower() == PIXEL_MAC:
            matches.append(name[1])
    matches = list(dict.fromkeys(matches))
    if len(matches) > 1:
        raise ValueError('Multiple interfaces match Pixel MAC; refusing to choose')
    return matches[0] if matches else None


def read_interfaces():
    return subprocess.check_output(['/usr/sbin/networksetup', '-listallhardwareports'], text=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--configure-link', action='store_true')
    p.add_argument('--listen', action='store_true')
    a = p.parse_args()
    interface = find_pixel(read_interfaces())
    if interface is None:
        print(json.dumps({'pixel_interface': None, 'status': 'Pixel CDC-ECM not enumerated'}))
        return 2
    print(json.dumps({'pixel_interface': interface, 'host_mac': PIXEL_MAC,
                      'host_ipv4': '192.168.7.1/24', 'phone_ipv4': '192.168.7.2',
                      'internet_sharing_configured_by_this_tool': False}), flush=True)
    if a.configure_link:
        # Recheck immediately before mutation. Never guess en5/en6 from ordering.
        if find_pixel(read_interfaces()) != interface:
            raise RuntimeError('Interface changed; refusing configuration')
        order = subprocess.check_output(['/usr/sbin/networksetup', '-listnetworkserviceorder'], text=True)
        services = re.findall(r'^\(\d+\) (.+)\n\(Hardware Port: [^\n]+, Device: ' + re.escape(interface) + r'\)$', order, re.M)
        if len(services) != 1:
            raise RuntimeError('Cannot uniquely identify the Pixel network service')
        subprocess.run(['/usr/sbin/networksetup', '-setmanual', services[0],
                        '192.168.7.1', '255.255.255.0'], check=True)
        subprocess.run(['/sbin/ping', '-c', '3', '-W', '1000', '192.168.7.2'], check=False)
    if a.listen:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
            listener.bind(('192.168.7.1', 5140))
            print('Listening for Pixel kernel log; Ctrl-C stops.', file=sys.stderr)
            while True:
                data, peer = listener.recvfrom(65535)
                if peer[0] == '192.168.7.2':
                    sys.stdout.write(data.decode('utf-8', 'replace'))
                    sys.stdout.flush()
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        pass
