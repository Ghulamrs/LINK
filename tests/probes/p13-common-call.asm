; p13, the other half: the same COMMON, and a call that pulls the member for its own sake.
; The member's cval = 7 is then in the image beside the COMMON, and the exit code says which
; of the two the reference reaches - the initialised definition or the linker's zero.
COMM    cval:DWORD
EXTERN  cfun:PROC
.CODE
start PROC
        sub     rsp, 40
        call    cfun
        mov     eax, DWORD PTR cval
        add     rsp, 40
        ret
start ENDP
END
