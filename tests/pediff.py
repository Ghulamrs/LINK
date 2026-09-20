#!/usr/bin/env python3
"""pediff.py - where two PE32+ images differ, region by region.

    python3 tests/pediff.py a.exe b.exe          the differing bytes, counted by region
    python3 tests/pediff.py -q a.exe b.exe       one line

The regions are read off the first image's headers: the DOS stub, the Rich header, the file and
optional headers (with the two time stamps and the checksum named on their own), the section
table, and each section by name - with .rdata split at the directories it holds (the IAT, the
import descriptors, the debug directory and the coffgrp record) so that a difference says what
it is in, not just where. Two images of different lengths are compared over the shorter and the
tail is counted as its own region."""
import struct, sys

def regions(d):
    r = []
    lf = struct.unpack('<I', d[0x3C:0x40])[0]
    r.append(('dos stub', 0, 0x80))
    r.append(('rich header', 0x80, lf))
    r.append(('file header', lf, lf + 24))
    r.append(('  time stamp', lf + 8, lf + 12))
    o = lf + 24
    r.append(('optional header', o, o + 112))
    r.append(('  checksum', o + 64, o + 68))
    nd = struct.unpack('<I', d[o + 108:o + 112])[0]
    r.append(('data directories', o + 112, o + 112 + nd * 8))
    nsec = struct.unpack('<H', d[lf + 6:lf + 8])[0]
    sh = o + 112 + nd * 8
    r.append(('section table', sh, sh + nsec * 40))
    dirs = {}
    names = ['export', 'import', 'resource', 'exception', 'security', 'basereloc', 'debug', 'arch',
             'globalptr', 'tls', 'loadconfig', 'bound', 'iat', 'delay', 'clr', 'reserved']
    for i in range(nd):
        rva, size = struct.unpack('<II', d[o + 112 + i * 8:o + 120 + i * 8])
        if rva:
            dirs[names[i]] = (rva, size)
    secs = []
    for i in range(nsec):
        h = d[sh + i * 40:sh + (i + 1) * 40]
        nm = h[:8].split(b'\0')[0].decode('latin1')
        vs, va, rs, rp = struct.unpack('<IIII', h[8:24])
        secs.append((nm, va, vs, rp, rs))
    for nm, va, vs, rp, rs in secs:
        if not rs:
            continue
        r.append((nm, rp, rp + rs))
        for dn, (rva, size) in sorted(dirs.items(), key=lambda x: x[1][0]):
            if va <= rva < va + vs:
                r.append(('  %s directory' % dn, rp + rva - va, rp + rva - va + size))
                if dn == 'debug':
                    for k in range(size // 28):
                        e = rp + rva - va + k * 28
                        typ = struct.unpack('<I', d[e + 12:e + 16])[0]
                        ptr = struct.unpack('<I', d[e + 24:e + 28])[0]
                        dsz = struct.unpack('<I', d[e + 16:e + 20])[0]
                        r.append(('    its time stamp', e + 4, e + 8))
                        if typ == 13:
                            r.append(('  debug record (coffgrp)', ptr, ptr + dsz))
                        elif typ == 16:
                            r.append(('  repro hash', ptr, ptr + dsz))
    return r

def diff(a, b):
    n = min(len(a), len(b))
    diffs = [i for i in range(n) if a[i] != b[i]]
    return diffs, n

def report(pa, pb, quiet=False):
    a = open(pa, 'rb').read(); b = open(pb, 'rb').read()
    if a == b:
        print('%s == %s (%d bytes)' % (pa, pb, len(a)))
        return 0
    lfa = struct.unpack('<I', a[0x3C:0x40])[0]; lfb = struct.unpack('<I', b[0x3C:0x40])[0]
    shift = lfb - lfa
    # when the Rich header is a different length the PE headers move with it: compare them
    # aligned, and count the move once, as the Rich header's
    soh_a = struct.unpack('<I', a[lfa + 24 + 60:lfa + 24 + 64])[0]
    soh_b = struct.unpack('<I', b[lfb + 24 + 60:lfb + 24 + 64])[0]
    if shift and soh_a == soh_b:
        # the block from the PE signature to the end of the section table, moved back into
        # place; what it displaces is the zero padding before the first section
        moved = b[lfb:soh_b]
        b = b[:lfa] + (moved + b'\0' * soh_a)[:soh_a - lfa] + b[soh_b:]
    diffs, n = diff(a, b)
    rs = regions(a)
    counts = []
    taken = set()
    for name, lo, hi in rs:
        c = [i for i in diffs if lo <= i < hi]
        if c:
            counts.append((name, len(c), c[0]))
            if not name.startswith(' '):
                taken.update(c)
    rest = [i for i in diffs if i not in taken]
    tail = abs(len(a) - len(b))
    total = len(diffs) + tail
    # the differences a reproducible build is allowed: the Rich header, the three copies of
    # the time stamp (file header, debug directory, coffgrp record's own entry) and the 32-byte
    # hash of a /Brepro image's second debug entry
    allowed = ('rich header', 'dos stub', '  time stamp', '    its time stamp', '  debug directory', '  repro hash')
    hard = [(nm, c) for nm, c, first in counts if not nm.startswith(' ') and nm not in ('rich header', 'dos stub')]
    hard_bytes = sum(c for nm, c in hard)
    stamps = sum(c for nm, c, first in counts if nm in ('  time stamp', '    its time stamp', '  debug directory', '  repro hash', 'file header'))
    if quiet:
        parts = ['%s %d' % (nm.strip(), c) for nm, c, first in counts if not nm.startswith(' ')]
        if shift:
            parts.insert(0, 'headers moved %+d' % shift)
        if tail:
            parts.append('length %+d' % (len(b) - len(a)))
        equiv = ''
        if not tail and all(nm in ('rich header', 'dos stub', 'file header', '.rdata') for nm, c in hard):
            rd = [c for nm, c, first in counts if nm == '.rdata']
            inner = sum(c for nm, c, first in counts if nm in ('  debug directory', '    its time stamp', '  repro hash'))
            fh = [c for nm, c, first in counts if nm == 'file header']
            if (not rd or rd[0] == inner) and (not fh or fh[0] <= 4):
                equiv = ' (equivalent: Rich header and build stamps only)'
        print('%d bytes differ: %s%s' % (total, ', '.join(parts), equiv))
        return total
    print('%s (%d bytes) against %s (%d bytes): %d bytes differ%s' % (pa, len(a), pb, len(b), total, (' (the PE headers moved %+d with the Rich header)' % shift) if shift else ''))
    for nm, c, first in counts:
        print('  %-28s %6d  first at 0x%X' % (nm, c, first))
    if rest:
        print('  %-28s %6d  first at 0x%X' % ('outside any region', len(rest), rest[0]))
    if tail:
        print('  %-28s %6d  (%s is longer)' % ('length', tail, pb if len(b) > len(a) else pa))
    return total

if __name__ == '__main__':
    args = [x for x in sys.argv[1:] if x != '-q']
    if len(args) != 2:
        print(__doc__); sys.exit(2)
    sys.exit(1 if report(args[0], args[1], '-q' in sys.argv) else 0)
