; p02: one import from one DLL. The smallest import directory there is - one descriptor, one
; thunk in the ILT, one in the IAT, one hint-name entry - and the thunk link.exe writes for the
; call, which the object only asks for as a REL32 to ExitProcess.
EXTERN ExitProcess:PROC
.CODE
start PROC
        sub     rsp, 40
        mov     ecx, 3
        call    ExitProcess
start ENDP
END
