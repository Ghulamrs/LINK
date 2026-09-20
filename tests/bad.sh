#!/bin/sh
# The other half of the bed. run.sh asks whether an image is right; this asks what the linker
# says when it will not make one - the refusals, and the two things it must not do silently.
# Nothing here needs a reference image or the Windows box: every input is made on the spot.
#
#   sh tests/bad.sh             (or `make test`, which runs run.sh first)
#
# Exit: 0 every case said what it should, 1 otherwise.

cd "$(dirname "$0")/.." || exit 1
LINK=${LINK:-build/link.exe}
OUT=${OUT:-build/test}
REF=tests/ref

[ -x "$LINK" ] || { echo "bad.sh: no linker at $LINK - run make first"; exit 1; }
mkdir -p "$OUT" || exit 1

fail=0

# one <name> <expected substring> -- <the linker's arguments>
# The case passes when the linker refuses and its output carries the substring.
one() {
    name=$1; want=$2; shift 3
    "$LINK" "$@" > "$OUT/bad-$name.log" 2>&1
    rc=$?
    got=$(cat "$OUT/bad-$name.log")
    if [ "$rc" -eq 0 ]; then
        printf '%-22s FAIL  linked, and should not have\n' "$name"; fail=$((fail+1)); return
    fi
    case $got in
    *"$want"*) printf '%-22s ok\n' "$name" ;;
    *)         printf '%-22s FAIL  wanted "%s", said: %s\n' "$name" "$want" "$got"; fail=$((fail+1)) ;;
    esac
}

: > "$OUT/empty.obj"
one empty-file "too short for a COFF header" -- /out:"$OUT/x.exe" /entry:start "$OUT/empty.obj"

# A section name of bytes no terminal should be asked to print, on a section whose raw data
# runs past the end of the file: the linker names the section in the message, and a name it
# read out of a broken object goes through a printable filter first (the review's L20).
python3 - "$REF/p01-bare.obj" "$OUT/rawname.obj" <<'PY'
import sys, struct
d = bytearray(open(sys.argv[1], 'rb').read())
optsz = struct.unpack('<H', d[16:18])[0]
sh = 20 + optsz                       # the first section header
d[sh:sh + 8] = b'\x01\x02\xff\xfe\x07\x00\x00\x00'
struct.pack_into('<I', d, sh + 16, 0x7000)     # a size that runs past the file
open(sys.argv[2], 'wb').write(bytes(d))
PY
one raw-section-name "runs past the file" -- /out:"$OUT/x.exe" /entry:start "$OUT/rawname.obj"
if LC_ALL=C grep -q '[^ -~	]' "$OUT/bad-raw-section-name.log"; then
    printf '%-22s FAIL  the message carries bytes that are not printable\n' raw-name-printable
    fail=$((fail+1))
else
    printf '%-22s ok\n' raw-name-printable
fi

# Two definitions of the same name, neither in a COMDAT: link.exe says LNK2005 and stops.
# This linker used to keep the first and say nothing (the review's B7).
one duplicate-definition "already defined" -- /out:"$OUT/x.exe" /entry:start /nodefaultlib \
    "$REF/p01-bare.obj" "$REF/p01-bare.obj"

# Every unresolved name, not just the first: p03 wants three and no library is given.
"$LINK" /out:"$OUT/x.exe" /entry:start /nodefaultlib "$REF/p03-imports.obj" \
    > "$OUT/bad-unresolved.log" 2>&1
n=$(grep -c -e GetStdHandle -e WriteFile -e ExitProcess "$OUT/bad-unresolved.log")
if [ "$n" -eq 3 ]; then
    printf '%-22s ok\n' unresolved-all
else
    printf '%-22s FAIL  wanted three names listed, got %s\n' unresolved-all "$n"; fail=$((fail+1))
fi

# A `/`-argument that names a file is a file, not a switch: on a POSIX host every absolute
# path looks like one of link.exe's switches (the review's L17).
if "$LINK" /out:"$OUT/abs.exe" /entry:start /nodefaultlib "$PWD/$REF/p01-bare.obj" \
        > "$OUT/bad-abspath.log" 2>&1 && [ -f "$OUT/abs.exe" ]; then
    printf '%-22s ok\n' absolute-path-input
else
    printf '%-22s FAIL  %s\n' absolute-path-input "$(head -1 "$OUT/bad-abspath.log")"; fail=$((fail+1))
fi

echo "---"
[ "$fail" -eq 0 ] && { echo "bad.sh: every case said what it should"; exit 0; }
echo "bad.sh: $fail case(s) did not"
exit 1
