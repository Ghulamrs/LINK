; p08 b: the other half, with data of its own, so the order both sections merge in can be read
PUBLIC helper_b
.DATA
mark_b  DQ 0B2B2B2B2B2B2B2B2h
.CODE
helper_b PROC
        mov     eax, 12
        ret
helper_b ENDP
END
