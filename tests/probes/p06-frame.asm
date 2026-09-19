; p06: two functions with declared prologues. On x64 the unwind data is not decoration - the
; RUNTIME_FUNCTIONs in .pdata must end up sorted by address and the .xdata they point at must
; survive the merge, or a stack walk through this code is wrong.
EXTERN ExitProcess:PROC
.CODE
worker PROC FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     eax, 11
        pop     rbp
        ret
worker ENDP
start PROC FRAME
        sub     rsp, 40
        .allocstack 40
        .endprolog
        call    worker
        mov     ecx, eax
        call    ExitProcess
start ENDP
END
