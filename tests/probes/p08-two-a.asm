; p08 a: calls across to the other object, and has data of its own before it. Two objects are
; the first case where the linker has to decide an order, not merely copy one.
EXTERN ExitProcess:PROC
EXTERN helper_b:PROC
.DATA
mark_a  DQ 0A1A1A1A1A1A1A1A1h
.CODE
start PROC
        sub     rsp, 40
        call    helper_b
        mov     ecx, eax
        call    ExitProcess
start ENDP
END
