#!/bin/sh
# The ratification corpus: every program under tests/corpus, taken through the two chains and
# compared. The Mac compiles (cc1i, cxx1i, shci write the MASM assembly here, since they are
# Mac binaries); the Windows box assembles each module twice - ml64, and this project's masm -
# and links each pair twice - link.exe on RIDE's exact line, and this linker built there by cl
# from src/ - then runs all four images. tests/windows/corpus.cmd is the box's half; this half
# ships the tree, brings the results back into build/corpus, and tests/corpus-report.py turns
# them into the table the review reads.
#
#   sh tests/corpus.sh              compile, ship, run on the box, report
#   sh tests/corpus.sh compile      compile only (no box)
#   sh tests/corpus.sh ship         ship what compile left and run the box's half
#   sh tests/corpus.sh report       report from what build/corpus already holds
#
# Each tests/corpus/NN-name/manifest names its modules in link order; the extension says which
# compiler: .c cc1i, .cpp cxx1i, .shl/.shm shci (the first only - shci compiles the files beside
# it into the same assembly), .asm as written (MASM's own corpus). A program with a .cpp links
# with cxx1i's /stack:8388608; one with Shalimar in it links RIDE's shmrt-x86_64-windows.lib.
set -u
cd "$(dirname "$0")/.." || exit 1
BIN=$(cd "${BIN:-../RIDE/bin}" && pwd)
CC1I=${CC1I:-$BIN/cc1i.exe}; CXX1I=${CXX1I:-$BIN/cxx1i.exe}; SHCI=${SHCI:-$BIN/shci.exe}
# Optimization flags for each compiler, so a corpus run can judge an optimizer
# end to end: cc1i's output assembled by this project's masm, linked by this
# linker, and run. Empty by default, which is -O0 and what the ledger records.
CC1FLAGS=${CC1FLAGS:-}; CXX1FLAGS=${CXX1FLAGS:-}; SHCFLAGS=${SHCFLAGS:-}
BOX=${BOX:-windows}
ROOT=${ROOT:-C:/link-probes/corpus}
T=${T:-build/corpus}
what=${1:-all}

compile() {
    rm -rf "$T"; mkdir -p "$T" || exit 1
    n=0; refused=0
    for d in tests/corpus/*/; do
        p=$(basename "$d"); out="$T/$p"; mkdir -p "$out"
        modules=$(sed -n 's/^modules=//p' "$d/manifest")
        cpp=0; shm=0; objs=""
        for m in $modules; do
            b=${m%.*}
            case $m in
            *.c)   "$CC1I" $CC1FLAGS -S -arch x86_64-windows -masm=masm -I "$d" "$d/$m" -o "$out/$b.asm" 2> "$out/$b.cc.err" < /dev/null || { echo "REFUSED $p/$m: $(grep -v '©' "$out/$b.cc.err" | head -1)"; refused=$((refused+1)); } ;;
            *.cpp) cpp=1; "$CXX1I" $CXX1FLAGS -arch x86_64-windows -masm=masm -S "$d/$m" -o "$out/$b.asm" 2> "$out/$b.cc.err" < /dev/null || { echo "REFUSED $p/$m: $(grep -v '©' "$out/$b.cc.err" | head -1)"; refused=$((refused+1)); } ;;
            *.shl|*.shm)
                   [ $shm = 1 ] && continue    # shci took the rest from beside the first
                   shm=1; ( cd "$d" && "$SHCI" $SHCFLAGS --target=x86_64-windows -S "$m" -o "$OLDPWD/$out/$b.asm" ) 2> "$out/$b.cc.err" < /dev/null || { echo "REFUSED $p/$m: $(grep -v '©' "$out/$b.cc.err" | head -1)"; refused=$((refused+1)); } ;;
            *.asm) cp "$d/$m" "$out/$b.asm" ;;
            esac
            [ -f "$out/$b.asm" ] && objs="$objs $b"
        done
        flags="-"; [ $cpp = 1 ] && flags="/stack:8388608"     # "-" for none: cmd's for /f folds empty fields
        # name | link flags beyond RIDE's common ones | objects in link order | shmrt yes/no | COMDAT yes/no
        # **ml64 has no syntax for COMDAT**, so a program whose assembly uses it - every
        # cxx1i program, and MASM's own COMDAT test - goes to the box marked, and ml64 is
        # not asked there: its refusal said nothing about this linker, run after run.
        comdat=0
        for o in $objs; do grep -qE 'COMDAT\(|ASSOCIATIVE\(' "$out/$o.asm" 2>/dev/null && comdat=1; done
        echo "$p|$flags|$objs|$shm|$comdat" > "$out/link.txt"
        n=$((n+1))
    done
    echo "corpus.sh: $n programs compiled, $refused modules refused"
}

ship() {
    find . -name "* [0-9].*" -delete
    COPYFILE_DISABLE=1 tar -C . --no-xattrs -czf "$T/tree.tgz" src tests/windows/corpus.cmd "$T"/[0-9]* || exit 1
    W=$(echo "$ROOT" | sed 's|/|\\|g')
    perl -e 'alarm 1800; exec @ARGV' ssh -n -o BatchMode=yes "$BOX" "if not exist $W mkdir $W" > /dev/null || exit 1
    scp -q "$T/tree.tgz" "$BOX:$ROOT/tree.tgz" || exit 1
    perl -e 'alarm 1800; exec @ARGV' ssh -n -o BatchMode=yes "$BOX" "cd /d $W & tar xzf tree.tgz & $W\\tests\\windows\\corpus.cmd $W" | tr -d '\r'
    scp -q "$BOX:$ROOT/results.tgz" "$T/results.tgz" || exit 1
    tar xzf "$T/results.tgz" || exit 1      # the members are build/corpus/... already
}

case $what in
compile) compile ;;
ship)    ship ;;
report)  python3 tests/corpus-report.py "$T" ;;
all)     compile; ship; python3 tests/corpus-report.py "$T" ;;
*)       echo "corpus.sh: compile | ship | report | (nothing)"; exit 2 ;;
esac
