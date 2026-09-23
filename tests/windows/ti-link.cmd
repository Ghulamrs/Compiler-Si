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
set linked=0
set failed=0
set refused=0
set expected=0
for %%f in (%ROOT%\tests\cases\*.shm %ROOT%\tests\load\*.shm) do (
    set NAME=%%~nf
    set KNOWN=
    if exist %NOLINK% findstr /b /c:"shm/!NAME! " %NOLINK% >nul 2>&1 && set KNOWN=1
    %BIN%\shalimar.exe --target=tms6747 -nologo -S %%f -o %OUT%\!NAME!.s > nul 2>&1
    if errorlevel 1 (set /a refused+=1) else if defined KNOWN (
        %BIN%\shalimar.exe --target=tms6747 -nologo %%f -o %OUT%\!NAME!.out > %OUT%\!NAME!.log 2>&1
        if errorlevel 1 (set /a expected+=1 & echo TI-NOLINK-AS-RECORDED !NAME!) else (set /a failed+=1 & echo LINKED-BUT-RECORDED-AS-NOLINK !NAME!)
    ) else (
        %BIN%\shalimar.exe --target=tms6747 -nologo %%f -o %OUT%\!NAME!.out > %OUT%\!NAME!.log 2>&1
        if errorlevel 1 (set /a failed+=1 & echo TI-FAILED !NAME! & type %OUT%\!NAME!.log) else (
            if exist %OUT%\!NAME!.out (set /a linked+=1) else (set /a failed+=1 & echo TI-NO-OUT !NAME!)
        )
    )
)
echo ti-link.cmd: %linked% programs linked by lnk6x, %failed% failed, %expected% do not link as ti-nolink.txt records, %refused% the compiler refuses
if not %failed%==0 exit /b 1
endlocal
