; p13: a COMMON symbol and the library search. COMM is a definition of a kind - a size and no
; bytes - and libcmt is full of them (_tls_index, __dyn_tls_init_callback,
; __scrt_ucrt_dll_is_in_use), each with a real definition in a member of the same library.
; Whether link.exe pulls that member for the COMMON alone is what the corpus turned on: the
; oracle's hello has none of tlssup.obj, tlsdyn.obj or ucrt_stubs.obj. The archive's member
; holds cval = 7 and the exit code is cval: 0 says the COMMON stood, 7 says the member came.
COMM    cval:DWORD
.CODE
start PROC
        mov     eax, DWORD PTR cval
        ret
start ENDP
END
