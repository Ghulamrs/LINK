# LINK

A linker for Windows x86-64: it reads the MS COFF objects `MASM` writes (and those `ml64` and
`cl` write) and produces a PE32+ executable Windows will load and run. `link.exe` is the oracle
throughout, as `ml64` is the assembler's - every layout decision here was read back from what
it does, and the tests hold this linker to it.

C-style ISO C++14, class-based, one thread per input file, no mutable globals.

Its sibling is `LNK6x`, the same design for the TMS320C6747: TI ELF objects in, an `lnk6x`-style
image out, driven by a linker command file. The two are separate programs, as `MASM` and `ASM6x`
are; what is shared is the design, not a library.

## Where things are

    src/link.h          what the passes say to each other
    src/coff.cpp        one MS COFF object, read
    src/archive.cpp     a .lib, and the short import members an import library is made of
    src/layout.cpp      resolution, placement, addresses, relocation
    src/image.cpp       the PE32+ file, and the three records the linker writes itself
    src/main.cpp        the command line, in link.exe's spelling
    tests/ref/          the objects, the archive and the images link.exe made: the bed's input
    tests/run.sh        links every probe again and compares the image byte for byte
    tests/probes/       the oracle probes: the smallest input that forces one PE feature each
    tests/windows/      what has to run on the Windows box (ml64, link, lib, dumpbin)
    tests/probes.sh     ships the probes to the box, runs them, brings the results back
    docs/pe-observed.md what link.exe does, read off the bed
    docs/known.md       what this linker does differently, or not at all

## Building and testing

    make                                    -> build/link.exe
    make test                               -> tests/run.sh

The test is the comparison: each probe is linked again from the objects in `tests/ref` and the
image is held against link.exe's, byte for byte, with `/timestamp:` pinning the one field no
test could otherwise predict. `run.sh` exits 1 if an image differed and 2 if a probe had to be
skipped for want of an input.

One input is not ours to check in: `kernel32.lib` is Microsoft's, and p02 through p08 all link
against it. Until it is beside the objects only p01 can be compared here. One run of

    sh tests/probes.sh

copies it back into `tests/ref` along with everything else the box produces, and the whole bed
runs on the Mac from then on.

## The probes

Nothing in `tests/probes` tests this linker. They exist to record what `link.exe` produces, one
feature at a time, so the format is learned from bytes rather than from prose:

    p01-bare        code only: no imports, no data, no unwind - the floor of a PE image
    p02-import      one import: the smallest import directory, IAT and hint-name table
    p03-imports     three imports from one DLL: their order in the ILT and the IAT
    p04-sections    .CONST, .DATA and .DATA? - merge order, characteristics, raw vs virtual size
    p05-addr64      pointers in data, linked twice: /dynamicbase against /fixed, so .reloc shows
    p06-frame       PROC FRAME: the RUNTIME_FUNCTIONs of .pdata and the unwind info of .xdata
    p07-lib         a static library: which member is pulled in, and which is left behind
    p08-two         two objects: cross-object calls and the order sections merge in

COMDAT is not probed here. `ml64` has no syntax for it, so those objects have to come from this
project's own assembler or from `cl`; `MASM/tests/comdat` already links a pair of them.

## Running them

The Windows box does the work; the Mac only ships the tree and reads the results.

    sh tests/probes.sh          # needs the ssh alias `windows`

Everything lands in `build/probe`: the objects, the executables, the `.map` files, and a
`dumpbin /all` of each object and each image.
