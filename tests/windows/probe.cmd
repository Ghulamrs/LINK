@echo off
rem  The oracle probes, on the box: ml64 assembles every tests\probes\*.asm, link.exe links the
rem  images listed in links.txt, lib.exe makes the p07 archive, and dumpbin /all records each
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

rem  p07: an archive of two members, only one of which resolves anything
lib /nologo /out:p07.lib p07-lib-used.obj p07-lib-unused.obj > p07.lib.log 2>&1 || (echo LIB-FAILED & set fail=1)
if exist p07.lib dumpbin /nologo /all p07.lib > p07.lib.txt
link %COMMON% /out:p07-lib.exe /map:p07-lib.map /entry:start /nodefaultlib p07-lib-main.obj p07.lib kernel32.lib > p07-lib.link 2>&1 || (echo LINK-FAILED p07-lib & set fail=1)
if exist p07-lib.exe (
    dumpbin /nologo /all p07-lib.exe > p07-lib.exe.txt
    p07-lib.exe
    echo p07-lib rc=!errorlevel! > p07-lib.out
)

rem  the versions that made all this, so a difference later can be dated
ml64 2>&1 | findstr /C:"Version" > versions.txt
link 2>&1 | findstr /C:"Version" >> versions.txt
if %fail%==1 (echo PROBE-FAILED & exit /b 1)
echo PROBE-DONE
