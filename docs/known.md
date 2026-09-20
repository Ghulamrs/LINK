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
arise. A probe with two DLLs and five names each would settle it.

**The coffgrp contribution's tail: read off the bed.** link.exe's `.rdata$zzzdbg` run is the
coffgrp record - 4 plus the entries, which is what the debug directory's size says - and, in an
image with no `.data` section, sixteen zero bytes more. p01, p02, p06, p07, p10 and p11 have the
sixteen; p03, p04, p05, p08 and p09 have none, and those are exactly the images with a `.data`
section. p04 and p06 carry the same eleven names and the same 0xDC record and differ in just
this, so the rule is not the record's size or the number of entries. What the sixteen bytes are
*for* is still unread - nothing a loader consults is in them - but when they are written is not.
This linker follows it; writing them always left every later address in `.rdata` sixteen bytes
out in the five images that do not want them.

## Not implemented

These have no probe, so there is nothing to be faithful to yet. Each is a refusal, not a
silent wrong answer: the linker says so and stops.

  * COMDAT folding. `MASM/tests/comdat` has the objects for it; link.exe's `SELECT_ANY` and
    `SELECT_ASSOCIATIVE` have to be read off a probe before this linker guesses at them.
  * `/DEBUG` and a `.debug` section. `.debug$S` and `.debug$T` are read and left behind.
  * Exports: no `.edata`, no `/DLL`, no export directory.
  * Delay-loaded imports, TLS directory, load-config directory, resources.
  * `/MERGE`, `/SECTION`, `/ALIGN` beyond the defaults, `/STUB`, `/ORDER`.
  * Short import members whose name type is not `IMPORT_NAME` - an ordinal-only import, or one
    whose imported name differs from its symbol name.
  * Weak externals: the aux record is read and the fallback recorded, but nothing uses it.
  * The image checksum stays zero, as link.exe leaves it for an executable.

## Things that are this linker's own

  * `/timestamp:hex` sets the time-date stamp. link.exe stamps an image with the time of day,
    which no test can predict, so the bed pins the second the reference images carry
    (0x6AAEDFA0) and compares everything else.
