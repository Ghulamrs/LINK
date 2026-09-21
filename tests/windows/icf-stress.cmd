@echo off
rem  The icf branch under cl, plain and under cl's AddressSanitizer, linking the
rem  corpus over and over with stderr kept: the crash hunt with its inputs.
rem    scp src/* windows:C:/link-probes/icf/src/ ; scp tests/windows/icf-stress.cmd windows:C:/link-probes/icf/stress.cmd
rem    ssh -n windows "C:\link-probes\icf\stress.cmd <iterations>"
rem  Needs the corpus objects tests/windows/corpus.cmd leaves under C:\link-probes\corpus.
rem  2026-09-21: 40 iterations, 3,040 links (1,520 under ASan), 0 failures.
setlocal enabledelayedexpansion
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set N=%~1
if "%N%"=="" set N=10
set W=C:\link-probes\icf
cd /d %W%
if not exist obj mkdir obj
if not exist obja mkdir obja
cl /nologo /std:c++14 /W4 /WX /permissive- /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS /Fo:obj\ /Fe:link-icf.exe src\*.cpp > build-plain.log 2>&1
if errorlevel 1 (echo PLAIN-BUILD-FAILED & type build-plain.log & exit /b 1)
cl /nologo /std:c++14 /W4 /permissive- /O2 /Zi /EHsc /fsanitize=address /D_CRT_SECURE_NO_WARNINGS /Fo:obja\ /Fe:link-asan.exe src\*.cpp > build-asan.log 2>&1
if errorlevel 1 (echo ASAN-BUILD-FAILED & type build-asan.log & exit /b 1)
echo built both
set LIBS=libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib legacy_stdio_definitions.lib
set FULL=
for %%l in (%LIBS%) do set FULL=!FULL! "%%~$LIB:l"
set SHM=C:\Program Files\RIDE 4.0\bin\lib\shmrt-x86_64-windows.lib
set C=C:\link-probes\corpus\build\corpus
set crashes=0
set links=0
del /q hits.txt 2>nul
for /l %%i in (1,1,%N%) do (
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
    for %%x in (icf asan) do (
      set /a links+=1
      %W%\link-%%x.exe /nologo /subsystem:console !flags! /timestamp:6AB006D2 /out:%W%\t-%%x.exe !myobjs! !extra! !FULL! > %W%\err.txt 2>&1
      set rc=!errorlevel!
      if not "!rc!"=="0" (
        set /a crashes+=1
        echo === %%i %%~nd %%x rc=!rc! >> %W%\hits.txt
        type %W%\err.txt >> %W%\hits.txt
      )
    )
  )
)
cd /d %W%
echo STRESS-DONE links=%links% failures=%crashes%
