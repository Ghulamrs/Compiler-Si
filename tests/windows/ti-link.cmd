@echo off
rem  The tms6747 target taken all the way to a TI program by the driver, on the
rem  box: every case of tests/cases and tests/load the compiler accepts is
rem  compiled with `shalimar --target=tms6747`, which assembles what it wrote and
rem  the runtime's assembly (lib\shmrt-tms6747 beside it) with the asm6x.exe
rem  beside it, and links them with TI's lnk6x against rts6740_elf_eh.lib into
rem  <case>.out. The linker is the judge. shalimar.exe, asm6x.exe and the runtime
rem  are RIDE's, in its bin, built from the trees RStudio's tools/to-windows.sh
rem  relayed; the TI tools are CCS 7.4's.
rem    ti-link.cmd <Compiler-Si tree on the box> <RIDE bin>
setlocal enabledelayedexpansion
if "%~1"==":shard" goto :shard
if "%~2"=="" (echo ti-link.cmd: needs the tree root and RIDE's bin & exit /b 2)
set ROOT=%~1
set BIN=%~2
set SHALIMAR_TI=C:\ti\ccsv7\tools\compiler\ti-cgt-c6000_8.2.2
set SHALIMAR_TILIB=C:\Users\GRA\Documents\VM6747\tilib
if not exist %BIN%\shalimar.exe (echo ti-link.cmd: no shalimar.exe in %BIN% & exit /b 1)
if not exist %BIN%\asm6x.exe (echo ti-link.cmd: no asm6x.exe beside it & exit /b 1)
if not exist %BIN%\lib\shmrt-tms6747\Runtime.s (echo ti-link.cmd: no C6000 runtime in %BIN%\lib & exit /b 1)
if not exist %SHALIMAR_TI%\bin\lnk6x.exe (echo ti-link.cmd: no lnk6x under %SHALIMAR_TI% & exit /b 1)
if not exist %SHALIMAR_TILIB%\rts6740_elf_eh.lib (echo ti-link.cmd: no rts6740_elf_eh.lib - see Emulator/tests/ti.sh & exit /b 1)
set OUT=%ROOT%\tests\out-ti-link
if exist %OUT% rmdir /s /q %OUT%
mkdir %OUT%
rem  The programs TI's runtime cannot link, each with its reason, are the
rem  emulator's list - Emulator\tests\ti-nolink.txt, "shm/<name>" lines - and a
rem  case on it that does link is a failure here too, until its line goes.
set NOLINK=%ROOT%\..\Emulator\tests\ti-nolink.txt
rem  Six shards at once with par.cmd; each prints a marker line per case, and the counts are
rem  read off the markers once all six are done.
rem  Visual Studio's environment once, here: a compiler that finds it set runs its tools
rem  directly, where without it every asm6x and lnk6x call went through vcvars64.bat again.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
call "%~dp0par.cmd" 6 "%~f0" > %OUT%\shards.log
findstr /v /b /c:"=" %OUT%\shards.log
for /f %%n in ('findstr /b /c:"=LINKED " %OUT%\shards.log ^| find /c /v ""') do set linked=%%n
for /f %%n in ('findstr /b /c:"=FAILED " %OUT%\shards.log ^| find /c /v ""') do set failed=%%n
for /f %%n in ('findstr /b /c:"=EXPECTED " %OUT%\shards.log ^| find /c /v ""') do set expected=%%n
for /f %%n in ('findstr /b /c:"=REFUSED " %OUT%\shards.log ^| find /c /v ""') do set refused=%%n
echo ti-link.cmd: %linked% programs linked by lnk6x, %failed% failed, %expected% do not link as ti-nolink.txt records, %refused% the compiler refuses
if not %failed%==0 exit /b 1
endlocal
exit /b 0

:shard
set /a I=0
for %%f in (%ROOT%\tests\cases\*.shm %ROOT%\tests\load\*.shm) do (
    set /a I+=1, M=I %% %~3 + 1
    if !M!==%~2 call :one %%~nf %%f
)
exit /b 0

:one
%BIN%\shalimar.exe --target=tms6747 -nologo -S %2 -o %OUT%\%1.s > nul 2>&1
if errorlevel 1 (echo =REFUSED %1& exit /b 0)
set KNOWN=
if exist %NOLINK% findstr /b /c:"shm/%1 " %NOLINK% >nul 2>&1 && set KNOWN=1
%BIN%\shalimar.exe --target=tms6747 -nologo %2 -o %OUT%\%1.out > %OUT%\%1.log 2>&1
if defined KNOWN (
    if errorlevel 1 (echo =EXPECTED %1& echo TI-NOLINK-AS-RECORDED %1) else (echo =FAILED %1& echo LINKED-BUT-RECORDED-AS-NOLINK %1)
    exit /b 0
)
if errorlevel 1 (echo =FAILED %1& echo TI-FAILED %1& type %OUT%\%1.log& exit /b 0)
if exist %OUT%\%1.out (echo =LINKED %1) else (echo =FAILED %1& echo TI-NO-OUT %1)
exit /b 0
