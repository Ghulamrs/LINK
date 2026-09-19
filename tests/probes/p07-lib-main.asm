; p07 main: it calls one of the two functions the library holds. What the image ends up with
; says how link.exe walks an archive - the member that resolves something is pulled in whole,
; the other is never opened.
EXTERN ExitProcess:PROC
EXTERN lib_used:PROC
.CODE
start PROC
        sub     rsp, 40
        call    lib_used
        mov     ecx, eax
        call    ExitProcess
start ENDP
END
