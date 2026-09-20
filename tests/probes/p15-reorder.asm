; p15: p03's three names from one DLL, referred to in the opposite order. p03 read
; GetStdHandle, WriteFile, ExitProcess and link.exe wrote WriteFile, ExitProcess,
; GetStdHandle - the first-referenced name last. If that is the rule, this probe's ILT comes
; out WriteFile, GetStdHandle, ExitProcess; if the order is the object's symbol table, or the
; archive's, or anything else, it does not. One probe, one prediction.
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
        xor     ecx, ecx
        call    ExitProcess             ; first referenced, where p03 called it last
        mov     ecx, -11
        call    GetStdHandle
        mov     rcx, rax
        lea     rdx, msg
        mov     r8d, 6
        lea     r9, written
        mov     qword ptr [rsp+32], 0
        call    WriteFile
        ret
start ENDP
END
