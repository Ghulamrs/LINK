#!/bin/sh
# cpp11's programs through this linker and link.exe, on the box (review V7) - see tests/cpp11/README.
# Ships src and tests, runs tests/windows/cpp11.cmd there, brings the outputs back into build/cpp11
# and compares each program's run under this linker with <name>.expected (or its first module's).
#
#   sh tests/cpp11.sh            (ssh alias `windows`; MASMEXE names the box's masm.exe)
cd "$(dirname "$0")/.." || exit 1
BOX=${BOX:-windows}
ROOT=${ROOT:-C:/link-cpp11}
T=${T:-build/cpp11}
mkdir -p "$T" || exit 1
COPYFILE_DISABLE=1 tar -C . --no-xattrs -czf "$T/tree.tgz" src tests || exit 1
W=$(echo "$ROOT" | sed 's|/|\\|g')
ssh -n -o BatchMode=yes "$BOX" "if not exist $W mkdir $W" > /dev/null || exit 1
scp -q "$T/tree.tgz" "$BOX:$ROOT/tree.tgz" || exit 1
M=""; [ -n "${MASMEXE:-}" ] && M="set MASMEXE=$MASMEXE& "
ssh -n -o BatchMode=yes "$BOX" "cd /d $W & (if exist src rmdir /s /q src) & (if exist tests rmdir /s /q tests) & tar xzf tree.tgz & $M$W\\tests\\windows\\cpp11.cmd $W" > "$T/box.log" 2>&1
rc=$?
grep -v "^$" "$T/box.log" | tr -d '\r'
scp -q "$BOX:$ROOT/build/cpp11/*.out" "$BOX:$ROOT/build/cpp11/*.pediff" "$T/" 2>/dev/null
fail=0; [ $rc = 0 ] || fail=1
sed 's/;.*//' tests/cpp11/programs.txt | while IFS='|' read -r name libs mods; do
    [ -n "$name" ] || continue
    e=tests/cpp11/$name.expected; [ -f "$e" ] || e=tests/cpp11/$(echo $mods | cut -d' ' -f1).expected
    if [ -f "$T/$name.mine.out" ] && tr -d '\r' < "$T/$name.mine.out" | cmp -s - "$e"; then
        printf '%-32s ok    %s\n' "$name" "$(tr -d '\r' < "$T/$name.pediff" 2>/dev/null | head -1 | cut -c1-90)"
    else
        printf '%-32s FAIL  its run under this linker is not %s\n' "$name" "$e"; echo x >> "$T/failed"
    fi
done
[ -f "$T/failed" ] && { rm -f "$T/failed"; fail=1; }
[ $fail = 0 ] && echo "cpp11.sh: every program linked by both, and ran as recorded" || echo "cpp11.sh: FAILED"
exit $fail
