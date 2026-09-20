; p09: imports from two DLLs, five names each, referenced in an order of their own. p03 could
; not tell what orders the ILT and the IAT - three names from one DLL are too few points - and
; nothing in the bed says how the two DLLs' descriptors, thunks and hint/name blobs are laid
; against each other, nor which of them the loader is handed first.
EXTERN GetStdHandle:PROC
EXTERN WriteFile:PROC
EXTERN ExitProcess:PROC
EXTERN GetLastError:PROC
EXTERN Sleep:PROC
EXTERN GetDesktopWindow:PROC
EXTERN IsWindow:PROC
EXTERN CharUpperA:PROC
EXTERN GetKeyState:PROC
EXTERN MessageBeep:PROC
.CONST
msg     DB "two dlls", 10
.DATA
written DD 0
.CODE
start PROC
        sub     rsp, 56
        call    GetDesktopWindow
        mov     rcx, rax
        call    IsWindow                ; 1 when the desktop is a window, which it is
        mov     ebx, eax
        mov     ecx, -11
        call    GetStdHandle
        mov     rcx, rax
        lea     rdx, msg
        mov     r8d, 9
        lea     r9, written
        mov     qword ptr [rsp+32], 0
        call    WriteFile
        mov     ecx, ebx
        call    ExitProcess
        ; never reached: named so that the import exists without the call being made
        call    GetLastError
        xor     ecx, ecx
        call    Sleep
        lea     rcx, msg
        call    CharUpperA
        mov     ecx, 65
        call    GetKeyState
        xor     ecx, ecx
        call    MessageBeep
        ret
start ENDP
END
