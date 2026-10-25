#!/usr/bin/env python3
"""Gate the complete permanent CA65 N: allocation, not its file size."""
import argparse
import json
import re
import struct
from pathlib import Path


def measure(module, link_map):
    if module[:8] != b'\x01\x00o65\x00\x00\x08':
        raise ValueError('unsupported resident o65 header')
    tb, text, db, data, bb, bss, zb, zp, stack = struct.unpack_from('<9H', module, 8)
    if (tb, db, bb, zb, stack) != (0, text, text + data, 0, 0):
        raise ValueError('resident must have contiguous segments and no implicit stack')
    modules = [name for name in re.findall(r'^([^\s].*):$',
               link_map.split('Segment list:')[0], re.M) if name != 'Modules list']
    if len(modules) != 1 or Path(modules[0]).name != 'fnresident-asm.o':
        raise ValueError('resident must link only the hand-written CA65 object')
    # No software stack. The existing OS hardware stack is borrowed during a
    # call, not permanently reserved. Conservatively charge all 26 ZP bytes
    # declared to the shared loader even though only two are borrowed/restored.
    return dict(code=text, initialized_data=data, bss=bss, zero_page=zp,
                permanent_stack=0, memlo_reservation=text + data + bss,
                total=text + data + bss + zp, limit=3072)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--module', type=Path, required=True)
    parser.add_argument('--map', type=Path, required=True)
    args = parser.parse_args()
    report = measure(args.module.read_bytes(), args.map.read_text())
    print(json.dumps(report, sort_keys=True))
    if report['total'] > report['limit']:
        raise SystemExit('permanent Atari resident RAM exceeds 3072 bytes')


if __name__ == '__main__':
    main()
