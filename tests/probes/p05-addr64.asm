; p05: addresses stored in data (ADDR64, not the RIP-relative kind), which are what a base
; relocation is for. Linked twice - /dynamicbase and /fixed - so .reloc can be read with the
; table present and then with it stripped, and the image base's part in both is clear.
EXTERN ExitProcess:PROC
.DATA
here    DQ start
table   DQ here, table
.CODE
start PROC
        sub     rsp, 40
        mov     rax, here
        xor     ecx, ecx
        call    ExitProcess
start ENDP
END
