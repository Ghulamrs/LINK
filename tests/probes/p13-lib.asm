; p13, the archive's one member: cval initialised, and a function to pull it by
PUBLIC cval
PUBLIC cfun
.DATA
cval    DD      7
.CODE
cfun PROC
        ret
cfun ENDP
END
