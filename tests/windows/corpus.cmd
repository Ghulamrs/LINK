@echo off
rem  The ratification corpus on the box. For every program build\corpus\NN-name holds (one .asm
rem  per module, written by the compilers on the Mac, and link.txt naming the link):
rem    ml\    the modules assembled by ml64             my\    the same modules assembled by masm
rem    ml-link.exe   ml objects, link.exe   (the oracle)   my-link.exe   my objects, link.exe
rem    ml-ours.exe   ml objects, this linker               my-ours.exe   my objects, this linker
rem    ml-full.exe / my-full.exe: this linker again, the libraries named by full path and the
rem                  switches it does not know left out - so that the cause after the first shows
rem  Every link uses RIDE's exact line (/nologo /subsystem:console [/stack:8388608] /out:...
rem  objects [shmrt-x86_64-windows.lib] libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib
rem  legacy_stdio_definitions.lib), link.exe with /Brepro so that its two images can be compared
rem  byte for byte, this linker with a fixed /timestamp. Each image that exists is run and its
rem  output and exit code kept. This linker is built here first, by cl from src\, with the
rem  house flags. Usage: corpus.cmd <tree root>
setlocal enabledelayedexpansion
if "%~1"=="" (echo corpus.cmd: needs the tree root & exit /b 2)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo corpus.cmd: no vcvars64 & exit /b 1)
cd /d "%~1"
set ROOT=%CD%
set MASM=C:\Program Files\RIDE 4.0\bin\masm.exe
if not "%MASMEXE%"=="" set MASM=%MASMEXE%
if not exist "%MASM%" (echo corpus.cmd: no masm at %MASM% & exit /b 1)
set LIBS=libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib legacy_stdio_definitions.lib
set SHM=C:\Program Files\RIDE 4.0\bin\lib\shmrt-x86_64-windows.lib
if not "%SHMLIB%"=="" set SHM=%SHMLIB%
set STAMP=6AB006D2
rem  the same libraries by full path, for the leg that measures what comes after LIB search
set FULL=
for %%l in (%LIBS%) do set FULL=!FULL! "%%~$LIB:l"
echo libraries: !FULL!

rem  this linker, built by cl with the house flags: a build that is not clean is a finding
if not exist build\corpus\cl mkdir build\corpus\cl
cl /nologo /std:c++14 /W4 /WX /permissive- /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS /Fo:build\corpus\cl\ /Fe:build\corpus\link-cl.exe src\*.cpp > build\corpus\cl-build.log 2>&1
if errorlevel 1 (echo CL-BUILD-FAILED & type build\corpus\cl-build.log & exit /b 1)
set OURS=%ROOT%\build\corpus\link-cl.exe
echo built %OURS%
ml64 2>&1 | findstr Version > build\corpus\versions.txt
link 2>&1 | findstr Version >> build\corpus\versions.txt
cl 2>&1 | findstr Version >> build\corpus\versions.txt
dir "%MASM%" | findstr masm >> build\corpus\versions.txt

for /d %%d in (build\corpus\*) do if exist "%ROOT%\%%d\link.txt" (
    cd /d "%ROOT%\%%d"
    set name=%%~nd
    if not exist ml mkdir ml
    if not exist my mkdir my
    for /f "usebackq tokens=1,2,3,4 delims=|" %%a in ("link.txt") do (
        set flags=%%b
        set objs=%%c
        set shm=%%d
    )
    if "!flags!"=="-" set flags=
    set mlobjs=
    set myobjs=
    set asmfail=0
    for %%m in (!objs!) do (
        ml64 /nologo /c /Fo ml\%%m.obj %%m.asm > ml\%%m.log 2>&1 || (echo ML64-REFUSED !name! %%m & set asmfail=1)
        "%MASM%" /c /nologo /Fo my\%%m.obj %%m.asm > my\%%m.log 2>&1 || (echo MASM-REFUSED !name! %%m & set asmfail=1)
        set mlobjs=!mlobjs! ml\%%m.obj
        set myobjs=!myobjs! my\%%m.obj
    )
    set extra=
    if "!shm!"=="1" set extra="%SHM%"
    link /nologo /Brepro /subsystem:console !flags! /out:ml-link.exe /map:ml-link.map !mlobjs! !extra! %LIBS% > ml-link.log 2>&1 && (echo LINKED !name! ml-link) || (echo REFUSED !name! ml-link)
    link /nologo /Brepro /subsystem:console !flags! /out:my-link.exe /map:my-link.map !myobjs! !extra! %LIBS% > my-link.log 2>&1 && (echo LINKED !name! my-link) || (echo REFUSED !name! my-link)
    "%OURS%" /nologo /subsystem:console !flags! /timestamp:%STAMP% /out:ml-ours.exe !mlobjs! !extra! %LIBS% > ml-ours.log 2>&1 && (echo LINKED !name! ml-ours) || (echo REFUSED !name! ml-ours)
    "%OURS%" /nologo /subsystem:console !flags! /timestamp:%STAMP% /out:my-ours.exe !myobjs! !extra! %LIBS% > my-ours.log 2>&1 && (echo LINKED !name! my-ours) || (echo REFUSED !name! my-ours)
    "%OURS%" /nologo /subsystem:console /timestamp:%STAMP% /out:ml-full.exe !mlobjs! !extra! !FULL! > ml-full.log 2>&1 && (echo LINKED !name! ml-full) || (echo REFUSED !name! ml-full)
    "%OURS%" /nologo /subsystem:console /timestamp:%STAMP% /out:my-full.exe !myobjs! !extra! !FULL! > my-full.log 2>&1 && (echo LINKED !name! my-full) || (echo REFUSED !name! my-full)
    for %%x in (ml-link my-link ml-ours my-ours ml-full my-full) do (
        if exist %%x.exe (
            %%x.exe < nul > %%x.out 2>&1
            echo rc=!errorlevel! >> %%x.out
            dumpbin /nologo /headers %%x.exe > %%x.hdr 2>&1
        )
    )
)
cd /d "%ROOT%"
tar czf results.tgz --exclude=cl --exclude=*.asm --exclude=tree.tgz build/corpus
echo CORPUS-DONE
