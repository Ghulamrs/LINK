#!/usr/bin/env python3
"""corpus-report.py - the ratification table, from what tests/corpus.sh brought back.

    python3 tests/corpus-report.py build/corpus [-v]

For every program: how each of the legs fared - the assembler (ml64 / masm), link.exe on the
two sets of objects, this linker on the two sets, and this linker with the libraries by full
path - whether the image equals the oracle's byte for byte (or how many bytes differ and where,
via pediff.py), and whether it ran to the same output and exit code. The oracle is ml64 +
link.exe; where ml64 refused a module (it has no syntax for cpp11's COMDAT clauses) the oracle
is masm + link.exe and the table says so. Refusals are grouped by their first line at the end,
each with the count of programs it stops."""
import os, re, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pediff

LEGS = ['ml-link', 'my-link', 'ml-ours', 'my-ours', 'ml-full', 'my-full']

def read(p):
    try:
        return open(p, 'rb').read().decode('latin1').replace('\r', '')
    except IOError:
        return None

def first_error(text):
    if not text:
        return ''
    for l in text.splitlines():
        l = l.strip()
        if 'error' in l.lower() or l.startswith('link:'):
            return re.sub(r'^.*?(error LNK\d+: |link: )', r'\1', l)
    lines = [l for l in text.splitlines() if l.strip()]
    return lines[0].strip() if lines else ''

HEX = re.compile(r'\b[0-9A-F]{12,16}\b')

def norm_out(text):
    """a run's output with the things a run cannot repeat neutralised: 16-digit addresses"""
    if text is None:
        return None
    return HEX.sub('<addr>', text)

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'build/corpus'
    verbose = '-v' in sys.argv
    progs = sorted(d for d in os.listdir(root) if re.match(r'\d\d-', d) and os.path.isfile(os.path.join(root, d, 'link.txt')))
    rows = []
    refusals = collections.defaultdict(list)
    asmref = collections.defaultdict(list)
    totals = collections.defaultdict(collections.Counter)
    for p in progs:
        d = os.path.join(root, p)
        parts = open(os.path.join(d, 'link.txt')).read().strip().split('|')
        name, flags, objs, shm = parts[:4]
        comdat = len(parts) > 4 and parts[4] == '1'   # ml64 has no COMDAT: not asked
        objs = objs.split()
        kinds = set()
        for o in objs:
            for ext in ('.c', '.cpp', '.shl', '.shm', '.asm'):
                pass
        # which assembler refused what
        ml_ok = not comdat and all(os.path.exists(os.path.join(d, 'ml', o + '.obj')) for o in objs)
        my_ok = all(os.path.exists(os.path.join(d, 'my', o + '.obj')) for o in objs)
        if not ml_ok and not comdat:
            for o in objs:
                if not os.path.exists(os.path.join(d, 'ml', o + '.obj')):
                    asmref['ml64: ' + first_error(read(os.path.join(d, 'ml', o + '.log')))].append(p)
        if not my_ok:
            for o in objs:
                if not os.path.exists(os.path.join(d, 'my', o + '.obj')):
                    asmref['masm: ' + first_error(read(os.path.join(d, 'my', o + '.log')))].append(p)
        oracle = 'ml-link' if ml_ok and os.path.exists(os.path.join(d, 'ml-link.exe')) else 'my-link'
        oracle_exe = os.path.join(d, oracle + '.exe')
        oracle_out = norm_out(read(os.path.join(d, oracle + '.out')))
        cells = {}
        for leg in LEGS:
            exe = os.path.join(d, leg + '.exe')
            asm_ok = ml_ok if leg.startswith('ml') else my_ok
            if comdat and leg.startswith('ml'):
                cells[leg] = 'n/a (ml64 has no COMDAT)'; totals[leg]['n/a, COMDAT'] += 1
                continue
            if not asm_ok:
                cells[leg] = 'n/a (assembler)'; totals[leg]['n/a'] += 1
                continue
            if not os.path.exists(exe):
                msg = first_error(read(os.path.join(d, leg + '.log')))
                cells[leg] = 'refused'; totals[leg]['refused'] += 1
                refusals[(leg.split('-')[1], msg)].append(p)
                continue
            if leg == oracle:
                out = read(os.path.join(d, leg + '.out')) or ''
                rc = re.search(r'rc=(-?\d+)', out)
                cells[leg] = 'oracle, ran rc=%s' % (rc.group(1) if rc else '?')
                totals[leg]['oracle'] += 1
                continue
            if not os.path.exists(oracle_exe):
                cells[leg] = 'linked, no oracle'; totals[leg]['linked'] += 1
                continue
            a = open(exe, 'rb').read(); b = open(oracle_exe, 'rb').read()
            if a == b:
                img = 'identical'
                totals[leg]['identical'] += 1
            else:
                import io, contextlib
                buf = io.StringIO()
                with contextlib.redirect_stdout(buf):
                    pediff.report(exe, oracle_exe, quiet=True)
                img = buf.getvalue().strip()
                totals[leg]['differ'] += 1
            out = norm_out(read(os.path.join(d, leg + '.out')))
            if out is None:
                run = 'not run'
            elif out == oracle_out:
                run = 'ran the same'; totals[leg]['ran same'] += 1
            else:
                run = 'RAN DIFFERENTLY'; totals[leg]['ran differently'] += 1
            cells[leg] = img + '; ' + run
        rows.append((p, len(objs), flags.replace('-', ''), shm == '1', oracle, cells))

    print('| # | program | modules | ml64+link.exe | masm+link.exe | ml64+LINK | masm+LINK | ml64+LINK (libs by path) | masm+LINK (libs by path) |')
    print('|---|---|---|---|---|---|---|---|---|')
    for p, n, flags, shm, oracle, cells in rows:
        num, nm = p.split('-', 1)
        mods = '%d%s%s' % (n, ' +shmrt' if shm else '', ' ' + flags if flags else '')
        print('| %s | %s | %s | %s |' % (num, nm, mods, ' | '.join(cells[l] for l in LEGS)))
    print()
    print('Totals over %d programs:' % len(rows))
    for leg in LEGS:
        t = totals[leg]
        print('  %-10s %s' % (leg, ', '.join('%s %d' % (k, v) for k, v in sorted(t.items()))))
    print()
    if asmref:
        print('Assembler refusals (the leg is then not applicable):')
        for msg, ps in sorted(asmref.items(), key=lambda x: -len(x[1])):
            print('  %3d  %s' % (len(ps), msg))
    print('Linker refusals, by first message:')
    for (who, msg), ps in sorted(refusals.items(), key=lambda x: -len(x[1])):
        print('  %3d  %-6s %s' % (len(ps), who, msg))
        if verbose:
            print('       ' + ' '.join(ps))

if __name__ == '__main__':
    main()
