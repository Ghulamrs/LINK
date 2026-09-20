; p11: the same bare program under cxx1i's /stack:8388608 - the one switch RIDE's C++ link line
; adds to the common ones. What it changes in the optional header (reserve, and whether commit
; moves with it) is read off here.
.CODE
start PROC
        mov     eax, 11
        ret
start ENDP
END
