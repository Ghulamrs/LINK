; p16: a second object with no @comp.id, to be linked beside p10's pair. p10 gave the Rich
; header three entries - the ml64 object, the unmarked one and link itself - and their order
; (0103899C, 00000000, 0102899C) fits no key yet tried: not the id ascending or descending,
; not the product id, not the build, not the count, and not "ascending with adjacent pairs
; swapped", which is what the other nine images look like (docs/known.md). A fourth entry, and
; an unmarked count of two rather than one, are the two things the set is missing.
PUBLIC  second_unmarked
.CODE
second_unmarked PROC
        mov     eax, 2
        ret
second_unmarked ENDP
END
