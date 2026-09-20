#!/bin/sh
# The bed. Every link the oracle made in tests/probes/links.txt, made again by this linker
# from the same objects, and the image compared with link.exe's byte for byte. Nothing here
# needs the Windows box: the objects, the archive and the reference images are checked in
# under tests/ref, which is the whole point of keeping them.
#
#   sh tests/run.sh             (or `make test`, which builds first)
#
# Exit: 0 all compared and matched, 1 something differed, 2 nothing differed but a probe had
# to be skipped for want of an input - kernel32.lib is the one that is not ours to check in.
# `sh tests/probes.sh` on a Mac with the box's ssh alias brings it back, and then the whole
# bed runs here.

cd "$(dirname "$0")/.." || exit 1
LINK=${LINK:-build/link.exe}
REF=tests/ref
OUT=${OUT:-build/test}
# The time-date stamp is read off each reference image rather than pinned here by hand: link.exe
# stamps every image with the second it ran, so a probe run re-stamps the whole bed, and a
# constant written here went stale - and the bed all red - after every run. STAMP=hex overrides.
stamp_of() {
    lf=$(od -An -tu4 -j 60 -N 4 "$1" | tr -d ' ')
    od -An -tx4 -j $((lf + 8)) -N 4 "$1" | tr -d ' '
}

[ -x "$LINK" ] || { echo "run.sh: no linker at $LINK - run make first"; exit 1; }
mkdir -p "$OUT" || exit 1

fail=0; skip=0; pass=0

one() {
    name=$1; flags=$2; objs=$3
    args=""; missing=""
    for o in $objs; do
        [ -f "$REF/$o.obj" ] || missing="$missing $o.obj"
        args="$args $REF/$o.obj"
    done
    fl=""
    for f in $flags; do
        case $f in
        *.lib)  [ -f "$REF/$f" ] || missing="$missing $f"
                args="$args $REF/$f" ;;
        *)      fl="$fl $f" ;;
        esac
    done
    if [ ! -f "$REF/$name.exe" ]; then
        printf '%-18s SKIP  no reference image\n' "$name"; skip=$((skip+1)); return
    fi
    if [ -n "$missing" ]; then
        printf '%-18s SKIP  missing:%s\n' "$name" "$missing"; skip=$((skip+1)); return
    fi
    stamp=${STAMP:-$(stamp_of "$REF/$name.exe")}
    if ! "$LINK" /out:"$OUT/$name.exe" /timestamp:$stamp $fl $args > "$OUT/$name.log" 2>&1; then
        printf '%-18s FAIL  %s\n' "$name" "$(head -1 "$OUT/$name.log")"; fail=$((fail+1)); return
    fi
    if cmp -s "$OUT/$name.exe" "$REF/$name.exe"; then
        printf '%-18s ok    %s bytes\n' "$name" "$(wc -c < "$REF/$name.exe" | tr -d ' ')"
        pass=$((pass+1))
    else
        n=$(cmp -l "$OUT/$name.exe" "$REF/$name.exe" 2>/dev/null | wc -l | tr -d ' ')
        printf '%-18s DIFF  %s bytes differ (first: %s)\n' "$name" "$n" \
               "$(cmp "$OUT/$name.exe" "$REF/$name.exe" 2>&1 | sed 's/.*differ: //')"
        fail=$((fail+1))
    fi
}

# links.txt is the oracle's own list, so the bed and the probes cannot drift apart
sed 's/;.*//' tests/probes/links.txt | grep '|' | while IFS='|' read -r n f o; do
    echo "$n|$f|$o"
done > "$OUT/links"

while IFS='|' read -r n f o; do
    [ -n "$n" ] && one "$n" "$f" "$o"
done < "$OUT/links"

echo "---"
echo "run.sh: $pass matched, $fail differed, $skip skipped"
[ "$fail" -gt 0 ] && exit 1
[ "$skip" -gt 0 ] && exit 2
exit 0
