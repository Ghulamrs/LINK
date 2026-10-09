@echo off
rem  cpp11's programs through this linker and through link.exe (review V7, 2026-10-08): objects with
rem  exception tables, throw records, RTTI and COMDAT data, which no ml64 probe can make. Each line
rem  of tests\cpp11\programs.txt is  name | link libraries | modules ; every module's .asm is cpp11's
rem  -masm=masm output, assembled here by the project's masm (MASMEXE, else one beside RIDE).
rem  Both images are run; <name>.link.out and <name>.mine.out are their outputs, and pediff.py's
rem  one-line summary of where the images differ goes to <name>.pediff - recorded, not gated: a CRT
rem  image differs from link.exe's in layout by design (docs/known.md), and what is held is the run.
rem  Usage: cpp11.cmd <tree root>    -> <root>\build\cpp11, and CPP11-DONE or CPP11-FAILED
setlocal enabledelayedexpansion
if "%~1"=="" (echo cpp11.cmd: needs the tree root & exit /b 2)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo cpp11.cmd: no vcvars64 & exit /b 1)
cd /d "%~1"
if not exist build\cpp11\cl mkdir build\cpp11\cl
set MASM=%MASMEXE%
if "%MASM%"=="" for %%m in ("C:\Program Files\RIDE 5.1\bin\masm.exe" "C:\Program Files\RIDE 5.0\bin\masm.exe") do if "!MASM!"=="" if exist %%m set MASM=%%~m
if not exist "%MASM%" (echo cpp11.cmd: no masm - MASMEXE names one & exit /b 1)
echo masm: %MASM%
cl /nologo /MP /std:c++14 /W4 /WX /permissive- /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS /Fo:build\cpp11\cl\ /Fe:build\cpp11\link-cl.exe src\*.cpp > build\cpp11\cl-build.log 2>&1
if not exist build\cpp11\link-cl.exe (type build\cpp11\cl-build.log & echo CPP11-FAILED & exit /b 1)
set fail=0
for /f "usebackq eol=; tokens=1,2,3 delims=|" %%a in ("tests\cpp11\programs.txt") do call :program %%a "%%b" "%%c"
if %fail%==1 (echo CPP11-FAILED & exit /b 1)
echo CPP11-DONE
exit /b 0

rem  %1 the program, %2 its libraries, %3 its modules
:program
set P=%~1
set OBJS=
for %%m in (%~3) do (
    "%MASM%" /nologo /c /Fo build\cpp11\%%m.obj tests\cpp11\%%m.asm > build\cpp11\%%m.masm 2>&1 || (echo MASM-FAILED %%m& set fail=1& exit /b 0)
    set OBJS=!OBJS! build\cpp11\%%m.obj
)
link /nologo /subsystem:console /stack:8388608 /out:build\cpp11\%P%.link.exe %OBJS% %~2 > build\cpp11\%P%.link.log 2>&1 || (echo LINKEXE-FAILED %P%& set fail=1& exit /b 0)
build\cpp11\link-cl.exe /nologo /subsystem:console /stack:8388608 /out:build\cpp11\%P%.mine.exe %OBJS% %~2 > build\cpp11\%P%.mine.log 2>&1 || (echo LINK-FAILED %P%: & type build\cpp11\%P%.mine.log & set fail=1& exit /b 0)
build\cpp11\%P%.link.exe > build\cpp11\%P%.link.out 2>&1 < nul
set RL=%errorlevel%
build\cpp11\%P%.mine.exe > build\cpp11\%P%.mine.out 2>&1 < nul
set RM=%errorlevel%
fc /b build\cpp11\%P%.link.out build\cpp11\%P%.mine.out > nul || (echo RAN-DIFFERENTLY %P%& set fail=1)
if not %RL%==%RM% (echo EXIT-DIFFERS %P%: link.exe's %RL%, this linker's %RM%& set fail=1)
python tests\pediff.py -q build\cpp11\%P%.link.exe build\cpp11\%P%.mine.exe > build\cpp11\%P%.pediff 2>&1
echo %P%: both ran, rc=%RM%, %RL%
exit /b 0
