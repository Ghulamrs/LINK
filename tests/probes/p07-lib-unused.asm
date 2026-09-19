; p07, the member that is not - it carries data of its own, so its absence from the image is
; visible in the section sizes and not only in the symbol table
PUBLIC lib_unused
.DATA
unused_mark DQ 0CCCCCCCCCCCCCCCCh
.CODE
lib_unused PROC
        mov     rax, unused_mark
        ret
lib_unused ENDP
END
