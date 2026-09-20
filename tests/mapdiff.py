#!/usr/bin/env python3
"""mapdiff.py - two map files (link.exe's and this linker's /map:) held against each other by
what they say rather than where: the runs of contributions and their lengths, then the publics
and the statics as (name, lib:object) pairs (link.exe's `f` and `i` flags - function, folded
by /OPT:ICF - are read past). Addresses are left out on purpose - a run that is
sixteen bytes short moves every address after it, and the question is which run.

    mapdiff.py oracle.map ours.map [-v]      (-v: every differing name, not the first twenty)"""
import re, sys
from collections import OrderedDict

def read(path):
    runs = OrderedDict(); pubs = set(); stats = set(); where = 'head'
    for line in open(path, errors='replace'):
        line = line.rstrip('\r\n')
        if line.startswith(' Static symbols'): where = 'stat'; continue
        if 'Publics by Value' in line: where = 'pub'; continue
        m = re.match(r' ([0-9a-f]{4}):([0-9a-f]{8}) ([0-9a-f]{8})H (\S+)\s+(CODE|DATA)$', line)
        if m:
            runs[(m.group(1), m.group(4))] = runs.get((m.group(1), m.group(4)), 0) + int(m.group(3), 16); continue
        m = re.match(r' ([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{16})\s+(f\s+)?(i\s+)?(\S+)$', line)
        if m and where in ('pub', 'stat'):
            (pubs if where == 'pub' else stats).add((m.group(3), m.group(7)))
    return runs, pubs, stats

def by_module(pairs):
    d = {}
    for n, w in pairs: d.setdefault(w, []).append(n)
    return d

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    verbose = '-v' in sys.argv
    if len(args) != 2: print(__doc__); return 2
    (ra, pa, sa), (rb, pb, sb) = read(args[0]), read(args[1])
    limit = None if verbose else 20
    print('runs: %d in %s, %d in %s' % (len(ra), args[0], len(rb), args[1]))
    names = list(ra.keys()) + [k for k in rb if k not in ra]
    for k in names:
        a, b = ra.get(k), rb.get(k)
        if a != b:
            print('  %-4s %-28s %8s %8s' % (k[0], k[1], '-' if a is None else '%x' % a, '-' if b is None else '%x' % b))
    for title, A, B in (('publics', pa, pb), ('statics', sa, sb)):
        only_a, only_b = sorted(A - B), sorted(B - A)
        print('%s: %d and %d; %d only in the first, %d only in the second' % (title, len(A), len(B), len(only_a), len(only_b)))
        for label, only in (('  only in the first: ', only_a), ('  only in the second:', only_b)):
            d = by_module(only)
            for w in sorted(d):
                ns = sorted(d[w])
                shown = ns if limit is None else ns[:limit]
                print('%s %-36s %s%s' % (label, w, ' '.join(shown), '' if len(shown) == len(ns) else ' ... (%d)' % len(ns)))
    return 0

if __name__ == '__main__':
    sys.exit(main())
