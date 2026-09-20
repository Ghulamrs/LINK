; p10: an object with no @comp.id - the project's own assembler writes none - linked beside an
; ml64 object with nothing imported: three Rich entries (ml64, the unmarked, link.exe itself),
; the first odd count the bed has seen. docs/pe-observed.md records the pairwise swap of the
; entries and says the odd one out is unknown; this says. probe.cmd assembles this one file with
; masm as well as ml64, and the link takes the masm object.
.CODE
unmarked PROC
        mov     eax, 10
        ret
unmarked ENDP
END
