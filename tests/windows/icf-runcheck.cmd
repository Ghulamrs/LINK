@echo off
rem  Every corpus program linked by link-icf.exe, run, and its output held against
rem  the pre-ICF linker's (my-full.out), with the two images' .text sizes.
rem    ssh -n windows "C:\link-probes\icf\runcheck.cmd"     (after icf-stress.cmd built link-icf.exe)
rem  2026-09-21: 37 same, 1 "differs" - 36-masm-b01 prints a pointer, which ASLR moves.
setlocal enabledelayedexpansion
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set W=C:\link-probes\icf
set LIBS=libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib legacy_stdio_definitions.lib
set FULL=
for %%l in (%LIBS%) do set FULL=!FULL! "%%~$LIB:l"
set SHM=C:\Program Files\RIDE 4.0\bin\lib\shmrt-x86_64-windows.lib
set C=C:\link-probes\corpus\build\corpus
set same=0
set diff=0
for /d %%d in (%C%\*) do if exist "%%d\link.txt" (
    cd /d "%%d"
    for /f "usebackq tokens=1,2,3,4 delims=|" %%a in ("link.txt") do (
        set flags=%%b
        set objs=%%c
        set shm=%%d
    )
    if "!flags!"=="-" set flags=
    set myobjs=
    for %%m in (!objs!) do set myobjs=!myobjs! my\%%m.obj
    set extra=
    if "!shm!"=="1" set extra="%SHM%"
    %W%\link-icf.exe /nologo /subsystem:console !flags! /timestamp:6AB006D2 /out:my-icf.exe !myobjs! !extra! !FULL! > my-icf.log 2>&1
    if exist my-icf.exe (
        my-icf.exe < nul > my-icf.out 2>&1
        echo rc=!errorlevel! >> my-icf.out
        fc /b my-icf.out my-full.out > nul && (set /a same+=1) || (set /a diff+=1 & echo DIFFERS %%~nd)
        for /f "tokens=1,2" %%s in ('dumpbin /nologo /headers my-icf.exe ^| findstr /c:"virtual size" ^| findstr /n . ^| findstr "^1:"') do set ti=%%t
        for /f "tokens=1,2" %%s in ('dumpbin /nologo /headers my-full.exe ^| findstr /c:"virtual size" ^| findstr /n . ^| findstr "^1:"') do set tf=%%t
        echo %%~nd text-full=!tf! text-icf=!ti!
    ) else (echo NO-IMAGE %%~nd)
)
echo RUNCHECK-DONE same=%same% differ=%diff%
