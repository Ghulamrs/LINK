; p01: code and nothing else - no imports, no data, no unwind. The floor of a PE image: whatever
; link.exe writes here is what every executable costs before it does anything at all.
.CODE
start PROC
        xor     eax, eax
        ret
start ENDP
END
