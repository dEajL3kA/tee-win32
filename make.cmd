@echo off
cd /d "%~dp0"

if "%MSVC_PATH%"=="" (
	set "MSVC_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
)

set "SOLUTION_FILE=tee.sln"

if exist "%MSVC_PATH%\VC\Auxiliary\Build\vcvarsall.bat" (
	call "%MSVC_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x86
) else (
	echo vcvarsall.bat not found. Please check your MSVC_PATH variable!
	goto BuildError
)

REM ------------------------------------------------------------
REM CLEAN UP
REM ------------------------------------------------------------

if exist "%CD%\bin\" rmdir /S /Q "%CD%\bin"
if exist "%CD%\bin\" (
	echo Failed to clean up intermediate files!
	goto BuildError
)

if exist "%CD%\obj\" rmdir /S /Q "%CD%\obj"
if exist "%CD%\obj\" (
	echo Failed to clean up intermediate files!
	goto BuildError
)

REM ------------------------------------------------------------
REM BUILD EXECUTABLES
REM ------------------------------------------------------------

MSBuild.exe /property:Platform=x86 /property:Configuration=Release /target:rebuild "%CD%\%SOLUTION_FILE%"
if not "%ERRORLEVEL%"=="0" goto BuildError

MSBuild.exe /property:Platform=x64 /property:Configuration=Release /target:rebuild "%CD%\%SOLUTION_FILE%"
if not "%ERRORLEVEL%"=="0" goto BuildError

REM ------------------------------------------------------------
REM COPY FILES
REM ------------------------------------------------------------

if not exist "%CD%\out\" mkdir "%CD%\out"

if exist "%CD%\out\tee-x86.exe" del /F "%CD%\out\tee-x86.exe"
if exist "%CD%\out\tee-x64.exe" del /F "%CD%\out\tee-x64.exe"

copy /Y /B "%CD%\bin\Win32\Release\tee.exe" "%CD%\out\tee-x86.exe"
copy /Y /B "%CD%\bin\x64\.\Release\tee.exe" "%CD%\out\tee-x64.exe"

attrib +R "%CD%\out\tee-x86.exe"
attrib +R "%CD%\out\tee-x64.exe"

REM ------------------------------------------------------------
REM COMPLETED
REM ------------------------------------------------------------

echo.
echo Build completed.
echo.
pause
goto:eof

REM ------------------------------------------------------------
REM BUILD ERROR
REM ------------------------------------------------------------

:BuildError
echo.
echo Build has failed !!!
echo.
pause
