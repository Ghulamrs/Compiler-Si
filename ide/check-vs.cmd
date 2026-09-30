@echo off
REM Prove the binary this solution builds is the compiler: compile one example with
REM shalimar.exe - its runtime beside it, and cpp11.exe the solution built first - and
REM run what comes out. ml64 and link come from vcvars64.
setlocal
set HERE=%~dp0
set SHC=%HERE%x64\Release\shalimar.exe
set CASE=%HERE%..\examples\gcd.shm
if not exist "%SHC%" ( echo check-vs.cmd: build it first & exit /b 1 )
if not exist "%HERE%x64\Release\lib\shmrt-x86_64-windows.lib" ( echo check-vs.cmd: no runtime beside shalimar.exe & exit /b 1 )
if not exist "%HERE%x64\Release\lib\shmrt-tms6747\Runtime.s" ( echo check-vs.cmd: no C6000 runtime beside shalimar.exe & exit /b 1 )

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
"%SHC%" "%CASE%" -o "%HERE%x64\Release\case.exe"
if errorlevel 1 ( echo check-vs.cmd: shalimar refused it & exit /b 1 )
"%HERE%x64\Release\case.exe" | findstr /c:"is 6" >nul
if errorlevel 1 ( echo check-vs.cmd: the program did not print "is 6" & exit /b 1 )
echo check-vs.cmd: the example compiled and ran
endlocal
