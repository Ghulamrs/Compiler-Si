@echo off
rem The debuggers measured on the Windows box: each program in tests\m10 built by shc with -g
rem (the GNU spelling, clang's COFF assembler, link /debug - a PDB) and its C counterpart by
rem cl /Zi, both put through cdb by tests\m10\m10.py and compared. Run from the tree's root,
rem after build.bat has made shc.exe and the runtime:
rem
rem   tests\windows\m10.cmd w1 w2 w3
setlocal
if not "%VSCMD_ARG_TGT_ARCH%"=="x64" call :findvcvars
if errorlevel 1 exit /b 1
set PATH=%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x64;%PATH%
cd /d %~dp0..\..
if not exist tests\out-m10 mkdir tests\out-m10
cd tests\out-m10
set FAILED=0
for %%w in (%*) do (
    copy /y ..\m10\%%w.shl . >nul
    copy /y ..\m10\%%w.c . >nul
    ..\..\shc.exe -nologo -g --no-search %%w.shl -o %%w.exe
    if errorlevel 1 ( echo FAIL %%w ^(shc^) & set FAILED=1 ) else (
        cl /nologo /Zi /Od /Fe:%%wc.exe %%w.c /link /incremental:no >nul
        if errorlevel 1 ( echo FAIL %%w ^(cl^) & set FAILED=1 ) else (
            py ..\m10\m10.py compare --debugger cdb %%w.exe %%w.shl %%wc.exe %%w.c ..\m10\%%w.script
            if errorlevel 1 set FAILED=1
        )
    )
)
exit /b %FAILED%

:findvcvars
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" ( echo no vswhere - Visual Studio 2022 does not look installed & exit /b 1 )
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version [17.0^,18.0^) -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" ( echo no Visual Studio 2022 found & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b 0
