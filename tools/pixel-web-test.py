#!/usr/bin/env python3
"""Serve only fixed, public diagnostic text on the Mac's Pixel-sharing address."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from ipaddress import ip_address, ip_network
from pathlib import Path
import datetime

LOG = Path('out/pixel-web-test.log')
PAGES = {
 '/': ('text/html; charset=utf-8', b'<!doctype html><html><head><meta name="viewport" content="width=device-width"><title>Pixel USB test</title></head><body><h1>PIXEL WEB TEST</h1><p>If you can read this, Safari is drawing web content.</p><p>No JavaScript. No images. No internet needed.</p><a href="/plain">Plain text test</a></body></html>'),
 '/plain': ('text/plain; charset=utf-8', b'PIXEL PLAIN TEXT TEST\n\nSafari received this text directly from your Mac.\n\nIf this is visible, basic web text rendering works.\n'),
}

class TestHandler(BaseHTTPRequestHandler):
 def do_GET(self):
  if ip_address(self.client_address[0]) not in ip_network('192.168.2.0/24'):
   self.send_error(403); return
  path=self.path.split('?',1)[0]
  if path not in PAGES:
   self.send_error(404); return
  kind,body=PAGES[path]
  self.send_response(200)
  self.send_header('Content-Type',kind)
  self.send_header('Content-Length',str(len(body)))
  self.send_header('Cache-Control','no-store')
  self.end_headers(); self.wfile.write(body)
 def log_message(self,fmt,*args):
  line=f'{datetime.datetime.now().isoformat(timespec="seconds")} {self.client_address[0]} {fmt % args}\n'
  with LOG.open('a') as f:f.write(line)
  print(line,end='',flush=True)

if __name__=='__main__':
 print('Pixel-only fixed test page: http://192.168.2.1:8080/',flush=True)
 ThreadingHTTPServer(('192.168.2.1',8080),TestHandler).serve_forever()
