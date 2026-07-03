@echo off
REM Build OVRK Workout Reminder into a single .exe
REM Usage: double-click this file, or run  build.bat  from a terminal.

setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VCVARS%" (
  echo Could not find Visual Studio build tools at:
  echo   %VCVARS%
  echo Edit VCVARS in build.bat to point at your vcvars64.bat
  pause
  exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 (
  echo Failed to initialise the compiler environment.
  pause
  exit /b 1
)

cd /d "%~dp0"
if not exist "dist" mkdir dist

echo Compiling...
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE ^
   src\main.cpp ^
   /Fe:dist\ovrk-workout.exe ^
   /Fo:dist\ ^
   /link /SUBSYSTEM:WINDOWS ^
   user32.lib gdi32.lib shell32.lib

if errorlevel 1 (
  echo.
  echo BUILD FAILED.
  pause
  exit /b 1
)

echo.
echo BUILD OK  ->  dist\ovrk-workout.exe
echo You can copy that single .exe anywhere and just double-click it.
endlocal
