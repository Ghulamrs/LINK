; p04: the three ordinary kinds of data - read-only, initialised and uninitialised - so the
; sections they merge into, the characteristics each gets, and the gap between a section's raw
; size and its virtual size (the BSS is in the second, not the first) are all on show.
EXTERN ExitProcess:PROC
.CONST
ro      DQ 0AAAAAAAAAAAAAAAAh
.DATA
rw      DQ 0BBBBBBBBBBBBBBBBh
.DATA?
zeroes  DQ ?
buffer  DB 4096 DUP (?)
.CODE
start PROC
        sub     rsp, 40
        mov     rax, ro
        mov     rw, rax
        mov     zeroes, rax
        xor     ecx, ecx
        call    ExitProcess
start ENDP
END
