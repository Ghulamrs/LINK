#!/bin/sh
# The oracle probes: ship tests\probes to the Windows box, have ml64, link, lib and dumpbin do
# their work there, and bring everything back - the objects, the images, the maps and the dumps.
# This runs nothing of this project's own; it records what link.exe does, so the linker can be
# written against bytes rather than against prose.
#
#   sh tests/probes.sh          (ssh alias `windows`, VS 2022 at its usual place on the box)
cd "$(dirname "$0")/.." || exit 1
BOX=${BOX:-windows}
ROOT=${ROOT:-C:/link-probes}
T=${T:-build/probe}
mkdir -p "$T" || exit 1
find . -name "* [0-9].*" -delete
COPYFILE_DISABLE=1 tar -C . --no-xattrs -czf "$T/tree.tgz" tests || exit 1
W=$(echo "$ROOT" | sed 's|/|\\|g')        # the same place in cmd's spelling
ssh -n -o BatchMode=yes "$BOX" "if not exist $W mkdir $W" > /dev/null || exit 1
scp -q "$T/tree.tgz" "$BOX:$ROOT/tree.tgz" || exit 1
ssh -n -o BatchMode=yes "$BOX" "cd /d $W & tar xzf tree.tgz & $W\\tests\\windows\\probe.cmd $W"
rc=$?
scp -q "$BOX:$ROOT/build/probe/*" "$T/" || exit 1
[ $rc = 0 ] || echo "probes.sh: the box reported a failure - read the .ml64, .link and .lib.log files"
# the objects, the archive, the import library and the images the linker is held to: the bed
# under tests/ref is what `make test` reads, and it is only as current as the last probe run
for f in "$T"/*.obj "$T"/*.exe "$T"/*.exe.txt "$T"/p07.lib "$T"/kernel32.lib; do
    [ -e "$f" ] && cp "$f" tests/ref/
done

for f in "$T"/*.exe; do
    [ -e "$f" ] || continue
    printf '%-20s %8d bytes\n' "$(basename "$f")" "$(wc -c < "$f")"
done
echo "probes.sh: results in $T"
exit $rc
