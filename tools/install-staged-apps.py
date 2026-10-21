#!/usr/bin/env python3
"""Install the IPSW's staged system apps into /Applications of a mounted root.

  install-staged-apps.py MOUNTED_ROOT

iOS 16 ships Calculator, Weather, Notes, Maps, Mail, Music and ~26 others in
/private/var/staged_system_apps; DataMigration's SystemAppMigrator installs
them on first boot. sweet-root-tweaks.py skips DataMigration, so they never
appear. This moves each staged .app into /Applications (as a non-removable
system app) and rewrites the matching /a40fix.manifest lines so the fixer
boot gives them root:wheel ownership with their original modes.
Run on a root that has not had its fixer boot yet. Idempotent.
"""
import os
import sys

root = sys.argv[1]
staged = os.path.join(root, 'private/var/staged_system_apps')
apps = os.path.join(root, 'Applications')
manifest = os.path.join(root, 'a40fix.manifest')
old_prefix = '/private/var/staged_system_apps/'

moved = []
for name in sorted(os.listdir(staged)):
	if not name.endswith('.app'):
		continue
	dst = os.path.join(apps, name)
	if os.path.lexists(dst):
		sys.exit(f'{dst} already exists; refusing to overwrite')
	os.rename(os.path.join(staged, name), dst)
	moved.append(name)

lines = open(manifest).read().splitlines()
out = []
rewritten = 0
for line in lines:
	uid, gid, mode, path = line.split(' ', 3)
	if path.startswith(old_prefix) and path[len(old_prefix):].split('/')[0].endswith('.app'):
		path = '/Applications/' + path[len(old_prefix):]
		uid, gid = '0', '0'
		rewritten += 1
	out.append(f'{uid} {gid} {mode} {path}')
with open(manifest, 'w') as f:
	f.write('\n'.join(out) + '\n')

print(f'moved {len(moved)} apps: {" ".join(moved)}')
print(f'manifest: {rewritten} entries rewritten to /Applications')
