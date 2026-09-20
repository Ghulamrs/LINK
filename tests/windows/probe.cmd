@echo off
rem  The oracle probes, on the box: ml64 assembles every tests\probes\*.asm, link.exe links the
rem  images listed in links.txt, lib.exe makes the archives libs.txt names, and dumpbin /all records each
rem  object and each image. Nothing of this project's own is built or tested here - the point is
rem  only to write down what Microsoft's tools do, byte for byte, for the Mac side to read.
rem  Usage: probe.cmd <tree root>    -> <root>\build\probe
setlocal enabledelayedexpansion
if "%~1"=="" (echo probe.cmd: needs the tree root & exit /b 2)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo probe.cmd: no vcvars64 & exit /b 1)
cd /d "%~1"
if not exist build\probe mkdir build\probe
cd build\probe
del /q *.* 2>nul
set PROBES=..\..\tests\probes
set COMMON=/nologo /subsystem:console
set fail=0

rem  assemble: one object per probe, and a dumpbin of it - the linker's input, as ml64 writes it
for %%f in (%PROBES%\*.asm) do (
    ml64 /nologo /c /Fo %%~nf.obj %%f > %%~nf.ml64 2>&1 || (echo ML64-FAILED %%~nf & set fail=1)
    if exist %%~nf.obj dumpbin /nologo /all %%~nf.obj > %%~nf.obj.txt
)

rem  p10 takes an object the project's own assembler wrote - one with no @comp.id - so that the
rem  Rich header's count of entries comes out odd. RIDE's masm is used, or MASMEXE names one.
set MASM=C:\Program Files\RIDE 4.0\bin\masm.exe
if not "%MASMEXE%"=="" set MASM=%MASMEXE%
rem  p16 wants a second unmarked object, so that the Rich header has an unmarked count of two.
for %%u in (p10-unmarked p16-rich-pair) do (
    if exist "%MASM%" ("%MASM%" /c /nologo /Fo %%u-my.obj %PROBES%\%%u.asm > %%u-my.masm 2>&1 || (echo MASM-FAILED %%u & set fail=1)) else (echo NO-MASM %%u)
    if exist %%u-my.obj dumpbin /nologo /all %%u-my.obj > %%u-my.obj.txt
)

rem  archives: each line of libs.txt is name | members, made before the links that name them
for /f "usebackq tokens=1,2 delims=|" %%a in ("%PROBES%\libs.txt") do (
    set mems=
    for %%o in (%%b) do set mems=!mems! %%o.obj
    lib /nologo /out:%%a.lib !mems! > %%a.lib.log 2>&1 || (echo LIB-FAILED %%a & set fail=1)
    if exist %%a.lib dumpbin /nologo /all %%a.lib > %%a.lib.txt
)

rem  link: each line of links.txt is name | extra flags | objects
for /f "usebackq tokens=1,2,* delims=|" %%a in ("%PROBES%\links.txt") do (
    set objs=
    for %%o in (%%c) do set objs=!objs! %%o.obj
    link %COMMON% /out:%%a.exe /map:%%a.map %%b !objs! > %%a.link 2>&1 || (echo LINK-FAILED %%a & set fail=1)
    if exist %%a.exe (
        dumpbin /nologo /all %%a.exe > %%a.exe.txt
        %%a.exe > %%a.out 2>&1
        echo %%a rc=!errorlevel! >> %%a.out
    )
)

rem  the import library the probes link against. It is Microsoft's, not this project's, so it
rem  is not checked in - but the Mac side cannot link p02..p08 without it, and copying it back
rem  beside the objects is what lets tests\run.sh compare every image rather than one.
for %%l in (kernel32.lib user32.lib advapi32.lib) do copy /y "%%~$LIB:l" . >nul 2>&1 || echo NO-%%l

rem  the versions that made all this, so a difference later can be dated
ml64 2>&1 | findstr /C:"Version" > versions.txt
link 2>&1 | findstr /C:"Version" >> versions.txt
if %fail%==1 (echo PROBE-FAILED & exit /b 1)
echo PROBE-DONE
