# What this linker does not do, and what it does differently

The bed compares images byte for byte, so anything deliberate or unexplained has to be written
down rather than left for a later reader to rediscover. This is that list. It is the linker's
answer to MASM's `known.txt`.

## Differences from link.exe on the probe bed

**p03's ILT and IAT are in a different order.** Three imports from one DLL: the hint/name blobs
of `.idata$6` come out in the order the object refers to them - GetStdHandle, WriteFile,
ExitProcess - and this linker puts the `.idata$4` and `.idata$5` words in that same order,
because both come from the same archive members pulled in the same pass. link.exe does not: it
writes the hint/name blobs in reference order and the ILT and IAT words in the order WriteFile,
ExitProcess, GetStdHandle. Nothing in the bed says what that order is - it is not alphabetical,
not by hint, and not by length - and with one DLL and three names there are too few points to
tell a rule from a coincidence. Every other probe imports one name, where the question does not
arise.

p09 was that probe - two DLLs, five names each - and it did not settle it. kernel32's words come
out WriteFile, ExitProcess, GetLastError, Sleep, GetStdHandle where the object refers to them
GetStdHandle first: the first-referenced name moved to the end, exactly as in p03. user32's come
out in reference order, GetDesktopWindow first, with nothing moved. So the first library's run
is rotated and the second's is not, and the member offsets in kernel32.lib are in neither order
(ExitProcess 0x2DA6A, GetLastError 0x33FC2, GetStdHandle 0x3742C, Sleep 0x47A30, WriteFile
0x4AD96). A rule that fits both would still be fitting two points. A probe with three DLLs, and
one with the first DLL's names referred to in another order, are what is left to try. Until
then this linker writes reference order throughout: p03 differs by nine bytes and p09 by ten in
the IAT, and both run, since every word still reaches its own hint/name blob.

**p14 and p15, run 2026-09-27, rule out reference order altogether.** p15 refers to p03's three
names in the opposite order - ExitProcess, GetStdHandle, WriteFile - and link.exe writes the same
ILT as p03's, WriteFile, ExitProcess, GetStdHandle: the order is the set's, not the references'.
And the set decides it as a whole, not name by name: in p14 kernel32's run is WriteFile,
GetStdHandle, ExitProcess, GetLastError - GetStdHandle second where p03 and p09 put it last - and
user32's puts IsWindow before GetDesktopWindow where p09 put it after. That reads as the order of
a hash table over the names in the link, which the bed cannot reconstruct from points. This linker
keeps reference order; p14 and p15 differ in the IAT for that reason.

**The Rich header: the entry values are settled, the order and the reserve are not.** p10 links
an ml64 object beside a masm object that carries no `@comp.id`, and link.exe writes three
entries - `0103899C` x1, `00000000` x1, `0102899C` x1. Two things follow. An *object* with no
`@comp.id` counts as `0x00000000`; `0x00010000` is the short import member's alone, and this
linker used to write `0x00010000` for both. That is now right.

The order is not. Across the thirteen reference images the observed precedence is consistent -
`01018179` before `00010000` before `0103899C` before `00000000` before `0102899C` - so some
fixed order over ids exists, but no key yet found produces it: not the id ascending or
descending, not the product id, not the build number, not the count, and "ascending, then
adjacent pairs swapped" (which fits the nine images with four entries) gives
`0102899C, 00000000, 0103899C` for p10 against link.exe's `0103899C, 00000000, 0102899C`.
This linker keeps the swap rule, so p10's three entries come out in the wrong order. p16 - two
unmarked objects beside one ml64 object - comes out the same way: `103899D` x1, `00000000` x2,
`102899D` x1, the fixed precedence again with the count changed and nothing else.

Nor is `e_lfanew` read. The reserve this linker computes - one slot per marked module, one for
all the unmarked together, one for itself - fits nine images and neither of the two added last:
p09 wants 5 slots where the rule says 8, p10 wants 5 where it says 3. The slack after `Rich` is
0 bytes in p01 and p11, 16 in p02-p06 and p10, 24 in p07 and p08, and 8 in p09, and it is not
an alignment: p01's block ends at 0xA8 and is not padded at all. Whatever link.exe reserves, it
reserves before it knows the entry list, and the bed has not shown what from.

**The coffgrp contribution's tail: read off the bed.** link.exe's `.rdata$zzzdbg` run is the
coffgrp record - 4 plus the entries, which is what the debug directory's size says - and, in an
image whose `.data` has no bytes in the file, sixteen zero bytes more. That was "no `.data`
section" until p13-a, whose `.data` holds only a COMMON symbol - raw size 0 - and has the
sixteen; all sixteen reference images agree with the corrected rule (2026-09-27). p01, p02, p06, p07, p10 and p11 have the
sixteen; p03, p04, p05, p08 and p09 have none, and those are exactly the images with a `.data`
section. p04 and p06 carry the same eleven names and the same 0xDC record and differ in just
this, so the rule is not the record's size or the number of entries. What the sixteen bytes are
*for* is still unread - nothing a loader consults is in them - but when they are written is not.
This linker follows it; writing them always left every later address in `.rdata` sixteen bytes
out in the five images that do not want them.

**The gap between two code contributions is int3.** link.exe fills the space alignment leaves
between contributions of a code section with `CC` and leaves the tail after the last one zero;
this linker wrote zeros throughout until 2026-09-27. p10, p12 and p13-b were the images with such
a gap, and each came down to its header bytes with it.

**The e_lfanew reserve, with the three images added since.** p12, p13-a and p13-b made sixteen
points, p14-p16 nineteen, and still no rule. Slack after `Rich`, then the entries (id x count):

    p01, p11      0   103899C x1, 102899C x1
    p12           0   103899C x2, 102899C x1
    p13-a        16   103899C x1, 102899C x1          three sections, .data raw size 0
    p13-b        24   103899C x2, 102899C x1          three sections
    p02-p06      16   01018179 x3, 00010000 x1 (x3 in p03), 103899C x1, 102899C x1
    p07, p08     24   as p02 with 103899C x2
    p09           8   01018179 x5, 00010000 x10, 103899C x1, 102899C x1
    p10          16   103899C x1, 00000000 x1, 102899C x1
    p14          16   01018179 x7, 00010000 x12, 103899D x1, 102899D x1
    p16          16   103899D x1, 00000000 x2, 102899D x1

p13-a and p01 have the same entries and differ by sixteen, so the reserve is not a function of
the entry list; p13 links against an archive whose member defines its COMMON symbol and is not
pulled, which suggests a count taken over modules read rather than modules kept. The bed's
remaining differences in p09, p10, p12 and p13 are this reserve, the Rich order above, p09's IAT
order, and p12's alternate names below.

**p12: an alternate name spelt `$fo$` is left unresolved.** `foo` has `/alternatename:foo=bar`
and an archive member defining it: link.exe pulls the member and calls it, as this linker does.
`mem_$fo$` has `/alternatename:mem_$fo$=mem_$fo_default$` and a member defining it too: link.exe
takes neither - the member is not pulled, the default is not used, and the call is relocated to
RVA 0. This linker takes the default. What link.exe reads the `$fo$` spelling as is not known.

## Not implemented

These have no probe, so there is nothing to be faithful to yet. Each is a refusal, not a
silent wrong answer: the linker says so and stops.

  * A `.pdb`. `/DEBUG` is accepted and says on stderr that it does nothing: `.debug$S` and
    `.debug$T` are read and left behind, and no CodeView entry is written.
  * Exports: no `.edata`, no `/DLL`, no export directory.
  * Delay-loaded imports, TLS directory, resources. `.tls$` is folded into `.data` where
    link.exe makes a `.tls` section and a TLS directory; nothing on the bed or in the corpus
    uses thread storage, so the fold is untested rather than known to be right.
  * `/MERGE`, `/SECTION`, `/ALIGN` beyond the defaults, `/STUB`, `/ORDER`.
  * Short import members whose name type is not `IMPORT_NAME` - an ordinal-only import, or one
    whose imported name differs from its symbol name.
  * The image checksum stays zero, as link.exe leaves it for an executable.

COMDAT selection, weak externals, COMMON symbols, `/DEFAULTLIB` and `/ALTERNATENAME` from
`.drectve`, the library search, the default entry point, `__ImageBase`, the sorted `.pdata` and
the load-config directory were all on this list and are now in; the corpus is what will say
whether they are right on more than the bed.

## Things that are this linker's own

  * `/timestamp:hex` sets the time-date stamp. link.exe stamps an image with the time of day,
    which no test can predict, so the bed pins the second the reference images carry
    (0x6AAEDFA0) and compares everything else.
