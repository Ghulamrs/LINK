; p03: three imports from one DLL, referenced in an order of their own, so the order link.exe
; puts them in the ILT and the IAT can be read off rather than guessed at.
EXTERN GetStdHandle:PROC
EXTERN WriteFile:PROC
EXTERN ExitProcess:PROC
.CONST
msg     DB "probe", 10
.DATA
written DD 0
.CODE
start PROC
        sub     rsp, 56
        mov     ecx, -11                ; STD_OUTPUT_HANDLE
        call    GetStdHandle
        mov     rcx, rax
        lea     rdx, msg
        mov     r8d, 6
        lea     r9, written
        mov     qword ptr [rsp+32], 0
        call    WriteFile
        xor     ecx, ecx
        call    ExitProcess
start ENDP
END
