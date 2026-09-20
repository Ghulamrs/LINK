; p14: three DLLs, four names each, referenced in a plainly recorded order. p03 showed the
; first library's ILT and IAT coming out with the first-referenced name moved to the end, and
; p09 showed the second library's coming out in reference order with nothing moved - two
; libraries behaving differently, which is not a rule (docs/known.md). A third library says
; whether "the first is rotated" or "the first name of a library is rotated" or neither.
; The reference order is kernel32, user32, advapi32, and inside each one it is as written.
EXTERN GetStdHandle:PROC
EXTERN WriteFile:PROC
EXTERN ExitProcess:PROC
EXTERN GetLastError:PROC
EXTERN GetDesktopWindow:PROC
EXTERN IsWindow:PROC
EXTERN CharUpperA:PROC
EXTERN MessageBeep:PROC
EXTERN GetUserNameA:PROC
EXTERN RegCloseKey:PROC
EXTERN IsTextUnicode:PROC
EXTERN GetSidSubAuthorityCount:PROC
.CONST
msg     DB "three dlls", 10
.DATA
written DD 0
.CODE
start PROC
        sub     rsp, 56
        mov     ecx, -11
        call    GetStdHandle            ; kernel32, first
        mov     rcx, rax
        lea     rdx, msg
        mov     r8d, 11
        lea     r9, written
        mov     qword ptr [rsp+32], 0
        call    WriteFile
        call    GetDesktopWindow        ; user32, first
        mov     rcx, rax
        call    IsWindow
        xor     ecx, ecx
        call    ExitProcess
        ; never reached: the remaining names exist as imports without being called
        call    GetLastError
        lea     rcx, msg
        call    CharUpperA
        mov     ecx, 0
        call    MessageBeep
        xor     ecx, ecx
        xor     edx, edx
        call    GetUserNameA            ; advapi32, first
        xor     ecx, ecx
        call    RegCloseKey
        xor     ecx, ecx
        xor     edx, edx
        call    IsTextUnicode
        xor     ecx, ecx
        call    GetSidSubAuthorityCount
        ret
start ENDP
END
