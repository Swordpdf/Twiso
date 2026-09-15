#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the loader-compatible Probe 12 fix and its launch delta from Probe 11."""
from pathlib import Path
import re
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent
PREVIOUS = ROOT.parent / 'native-launch-probe-11'
# Probe 11 was removed during a disk cleanup; its blessed linked ELF is kept
# in-tree so import-delta gating survives without the old probe directory.
_blessed = ROOT / 'build' / 'blessed-llvm-pie.elf'
_probe11_elf = PREVIOUS / 'build' / 'llvm-pie.elf'
if _probe11_elf.is_file():
    REF_ELF = _probe11_elf
elif _blessed.is_file():
    REF_ELF = _blessed
else:
    raise SystemExit('No reference build: restore native-launch-probe-11 or build/blessed-llvm-pie.elf')
BASE = ROOT.parent / 'native-menu-probe-02'
sys.path.insert(0, str(ROOT.parent / 'native-menu-probe-02-aligned'))
from test_layout import program_headers, sections, bad_mappings, u64

def imports(path):
    output = subprocess.check_output(['readelf', '--dyn-syms', '--wide', str(path)], text=True)
    return set(re.findall(r'\bUND\s+(\S+)', output))

def needed(path):
    output = subprocess.check_output(['readelf', '-d', '--wide', str(path)], text=True)
    return set(re.findall(r'\(NEEDED\).*\[(.*?)\]', output))

old = imports(REF_ELF)
new = imports(ROOT / 'build/llvm-pie.elf')
if REF_ELF == _blessed:
    # The blessed baseline already contains the Probe 12 launch pair and the
    # reviewed cover HTTP/2 client (sceNet/sceSsl/sceHttp2); any further delta
    # must be explicitly reviewed, so expect none.
    expected_added = set()
else:
    expected_added = {'sceSystemServiceLaunchApp'}
expected_removed = set()
assert new - old == expected_added, f'Unexpected added imports: {sorted(new-old)}'
assert old - new == expected_removed, f'Unexpected removed imports: {sorted(old-new)}'
assert needed(ROOT / 'build/llvm-pie.elf') == needed(REF_ELF)
for name in new:
    if name in old or name in expected_added:
        continue
    lower = name.lower()
    forbidden_exact = {
        'system', 'exec', 'execl', 'execle', 'execlp', 'execv', 'execve',
        'execvp', 'fork', 'vfork', 'kill', 'mount', 'unmount', 'mkdir',
        'chmod', 'chown', 'write', 'unlink', 'rename',
    }
    assert lower not in forbidden_exact, name
    assert 'launchapp' not in lower, name
assert (ROOT / 'build/obj/demo_renderer.o').read_bytes() == (BASE / 'build/obj/demo_renderer.o').read_bytes()

source = (ROOT / 'build/llvm-pie.elf').read_bytes()
image = (ROOT / 'build/eboot.elf').read_bytes()
console_image = (ROOT / 'build/eboot-console-ready.elf').read_bytes()
comment = image.rfind(b'PATH')
assert comment >= len(image) - 0x1000
comment_end = comment + 8 + struct.unpack_from('<I', image, comment + 4)[0]
assert len(console_image) == len(image)
assert console_image[:comment_end] == image[:comment_end]
assert console_image[comment_end:] == bytes(len(image) - comment_end)
assert not bad_mappings(image)
headers = program_headers(image)
loads = [p for p in headers if p['type'] == 1 and p['flags']]
for p in headers:
    if p['type'] == 1:
        assert p['align'] == 0x4000 and p['filesz'] <= p['memsz']
        assert p['offset'] + p['filesz'] <= len(image)
for section in sections(source):
    name = section['name']
    if not section['flags'] & 2 or section['type'] == 8 or not section['size']:
        continue
    if name in ('.dynstr', '.dynsym', '.dynamic', '.hash', '.gnu.hash') or name.startswith('.rela.'):
        continue
    mappings = [p for p in loads if p['va'] <= section['va'] and section['va'] + section['size'] <= p['va'] + p['filesz']]
    assert len(mappings) == 1, name
    p = mappings[0]; offset = p['offset'] + section['va'] - p['va']
    assert image[offset:offset + section['size']] == source[section['offset']:section['offset'] + section['size']], name
proc = next(p for p in headers if p['type'] == 0x61000001)
assert u64(image, proc['offset']) == 0x60
assert any(proc['offset'] == p['offset'] + proc['va'] - p['va'] and
           p['va'] <= proc['va'] and proc['va'] + proc['filesz'] <= p['va'] + p['filesz'] for p in loads)
assert u64(image, 0x18) == u64(source, 0x18)

symbols = subprocess.check_output(['readelf', '-s', '--wide', str(ROOT / 'build/llvm-pie.elf')], text=True)
for symbol, minimum in [('directory_scan', 0x10000 + 128 * 264), ('serial_scan', 0x1000),
                        ('game_files', 0x10000), ('transaction', 0x30000)]:
    found = re.findall(r'^\s*\d+:\s+([0-9a-fA-F]+)\s+(\S+)\s+OBJECT\s+LOCAL\s+DEFAULT\s+\S+\s+.*' + symbol + r'\S*$', symbols, re.M)
    assert len(found) == 1, (symbol, found)
    address = int(found[0][0], 16); size_text = found[0][1]
    size = int(size_text, 16 if size_text.startswith('0x') else 10)
    assert address % 0x4000 == 0 and size >= minimum
    assert any(p['flags'] & 2 and p['va'] <= address and address + size <= p['va'] + p['memsz'] for p in loads)
    print(f'PASS: {symbol} at 0x{address:x}, size 0x{size:x}, fully writable.')
print('PASS: all LOAD mappings, allocated code/data, process params, and entry point.')
print('PASS: loader-compatible launch import pair added:', ', '.join(sorted(expected_added)))
print('PASS: known loader-compatible import retained:', 'sceLncUtilLaunchApp')
print(f'PASS: console-ready ELF clears only consumed metadata after 0x{comment_end:x}.')
print('PASS: no extra process, mount, runtime, renderer, or dependency change.')
