# What link.exe does

Read off the probe bed, not out of the manual. Every number here came from
`build/probe/*.exe.txt` and the images beside them; the images and their dumps are checked in
under `tests/ref` so a later change can be held to them. Tools: ml64 and link 14.44.35228.0,
Windows box, 2026-09-19. All ten links were silent and every image ran with the return code its
probe was written to give.

## The floor of an image (p01)

1536 bytes (0x600), and **two** sections, not one. A bare `.text` of three bytes brings an
`.rdata` the linker writes on its own account:

    .text    virtual size 3       RVA 0x1000   raw 0x200 at file 0x200
    .rdata   virtual size 0x9C    RVA 0x2000   raw 0x200 at file 0x400

So "one object, one section in, one section out" is not what link.exe does, and a linker meant to
be byte-faithful has to either write that `.rdata` too or record the difference deliberately, the
way MASM's `known.txt` records the ones it does not chase.

### What is in that .rdata

Three contributions, in this order:

| at     | size | what |
|--------|------|------|
| 0x2000 | 0x1C | the debug directory - one `IMAGE_DEBUG_DIRECTORY`, type 13 (POGO; dumpbin calls it `coffgrp`) |
| 0x201C | 0x18 | `.rdata$voltmd` - the volatile-metadata record: `18 00 00 00`, `00 80 00 80`, then 16 zero bytes |
| 0x2034 | 0x68 | `.rdata$zzzdbg` - the record the debug directory points at |

The debug directory entry is the ordinary 28-byte structure: characteristics 0, the time-date
stamp (the same value as the file header's), version 0.0, type 13, then size, RVA and file
pointer of the record.

### The coffgrp record

A section-contribution map, and its shape is plain once decoded:

    4 bytes   zero
    then, per contribution:
      4 bytes   RVA
      4 bytes   size
      n bytes   the name, NUL-terminated, padded with NULs to a multiple of 4
    16 bytes  zero, at the end

p01's four entries are `.text$mn` (0x1000, 3), `.rdata` (0x2000, 0x1C), `.rdata$voltmd`
(0x201C, 0x18) and `.rdata$zzzdbg` (0x2034, 0x68) - the record describes itself, last.

## The header constants

Fixed across every image the bed produced:

    magic                 0x20B (PE32+)          machine        0x8664
    image base            0x140000000            characteristics 0x22 (executable, large address aware)
    section alignment     0x1000                 file alignment  0x200
    entry point RVA       0x1000                 base of code    0x1000
    OS / subsystem ver    6.00                   subsystem       3 (CUI)
    stack reserve/commit  0x100000 / 0x1000      heap            the same
    DllCharacteristics    0x8160                 directories     16
    checksum              0                      symbols         none

`0x8160` is high-entropy VA, dynamic base, NX compatible, terminal-server aware. Under `/fixed`
it becomes `0x8120` - dynamic base is the bit that goes - and the `.reloc` section goes with it.

### One constant that is not constant

`size of headers` is **0x200** in p01 and **0x400** in every other image, which moves the first
section's file offset with it. The only difference in p01's link line is that it names no library
at all. Whether it is the library on the command line or the imports that follow from it is not
yet known: `p01-bare-lib` links the same object with `kernel32.lib` present but nothing imported,
and settles it.

## Imports (p02)

One import, and still two sections - the `.idata$*` contributions merge into `.rdata`, but not
in one run:

    0x2000  .idata$5        0x10   the IAT: one 8-byte slot and its null terminator
    0x2010  .rdata          0x1C   the debug directory
    0x202C  .rdata$voltmd   0x18
    0x2044  .rdata$zzzdbg   0xCC   coffgrp
    0x2110  .idata$2        0x14   the import descriptor
    0x2124  .idata$3        0x14   the null descriptor
    0x2138  .idata$4        0x10   the INT
    0x2148  .idata$6        0x1C   hint/name: hint 0x186, "ExitProcess", then "KERNEL32.dll"

So the rule is not "$ order". `.idata$5` is lifted to the front of the section - the IAT starts
`.rdata`, and the IAT directory points at exactly it, RVA 0x2000 size 0x10 - and everything else
follows the linker's own `.rdata` contributions in `$` order. The import directory is 0x28: one
descriptor and the null one. Time-date stamp and forwarder chain are zero.

### The thunk

The object asks only for a REL32 to `ExitProcess`. The image answers with six bytes appended to
`.text`:

    140001009  E8 00 00 00 00       call 14000100E     (the call in the object, retargeted)
    14000100E  FF 25 EC 0F 00 00    jmp  qword ptr [rip+0xFEC] -> 140002000, the IAT slot

The linker synthesises the jump thunk and points the call at it. `.text` grows from 0xE to 0x14.

## The rest of the bed, at a glance

| probe | sections | image | what it settled |
|---|---|---|---|
| p03-imports | .text .rdata .data | 0x4000 | three imports from one DLL, in one descriptor |
| p04-sections | .text .rdata .data | 0x5000 | BSS folds into `.data`: virtual size 0x1018, raw 0x200 |
| p05-addr64-dyn | + .reloc | 0x5000 | one 0x10 block at RVA 0x3000: three DIR64 and one ABS pad |
| p05-addr64-fixed | no .reloc | 0x4000 | `/fixed` drops the section and the dynamic-base bit |
| p06-frame | .text .rdata .pdata | 0x4000 | `.pdata` keeps its own section; `.xdata` merges into `.rdata` |
| p07-lib | .text .rdata | 0x3000 | the unused member never arrives - its marker is not in the image |
| p08-two | .text .rdata .data | 0x4000 | both objects' `.text` and `.data` merge in command-line order |

p06 is worth a second look: two `RUNTIME_FUNCTION`s, sorted by start address, their unwind info
at 0x2130 and 0x2138 - inside `.rdata`, not in a section of its own. The exception directory is
0x18, which is the two entries and nothing else.

## What this decides for the linker

1. Section merging is by contribution name (`.text$mn`, `.rdata$voltmd`, `.idata$5`), not by
   section name, and the `$` suffix orders them - with `.idata$5` lifted out of that order.
2. `.xdata` and `.idata` are not output sections. `.pdata` and `.reloc` are.
3. BSS is virtual size beyond raw size in the section that swallowed it, never bytes on disk.
4. The debug directory, the volatile-metadata record and the coffgrp map are the linker's own
   output, present even when the input has nothing to say - the first real decision to make is
   whether this linker writes them, and `tests/ref` is what that decision will be judged against.
