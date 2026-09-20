; p12: /alternatename and the library search. Two names this object leaves undefined, each
; with an alternate defined right here, and an archive holding a definition of each. Which
; definition the image ends up with says whether link.exe searches the archives for a name
; that has an alternate. The corpus showed both answers: __guard_memcpy_fptr was pulled from
; softmemtag.obj though loadcfg.obj says /alternatename:__guard_memcpy_fptr=__AbsoluteZero,
; while memcpy_$fo$ never pulled overrides.obj from beside memcpy.obj's
; /alternatename:memcpy_$fo$=memcpy_$fo_default$. So both shapes are here: foo=bar is the
; plain case, mem_$fo$=mem_$fo_default$ the function-override spelling. The exit code is
; foo() * 16 + mem_$fo$(): an alternate answers 1, a member of the archive 2.
OPTION DOTNAME
EXTERN foo:PROC
EXTERN mem_$fo$:PROC
.drectve SEGMENT INFO
        DB      "/alternatename:foo=bar /alternatename:mem_$fo$=mem_$fo_default$ "
.drectve ENDS
.CODE
bar PROC
        mov     eax, 1
        ret
bar ENDP
mem_$fo_default$ PROC
        mov     eax, 1
        ret
mem_$fo_default$ ENDP
start PROC
        sub     rsp, 40
        call    foo
        shl     eax, 4
        mov     DWORD PTR [rsp+32], eax
        call    mem_$fo$
        add     eax, DWORD PTR [rsp+32]
        add     rsp, 40
        ret
start ENDP
END
