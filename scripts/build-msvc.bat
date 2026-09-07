@echo off
setlocal

rem Optional local file; it is Git-ignored so machine paths never enter the repo.
if exist "%~dp0toolchain.bat" call "%~dp0toolchain.bat"

if not defined MEMESERVANT2_VSDEVCMD (
    echo MEMESERVANT2_VSDEVCMD is not set. See scripts\toolchain.bat.example.
    exit /b 1
)
if not defined MEMESERVANT2_CMAKE (
    echo MEMESERVANT2_CMAKE is not set. See scripts\toolchain.bat.example.
    exit /b 1
)
if not defined MEMESERVANT2_NINJA (
    echo MEMESERVANT2_NINJA is not set. See scripts\toolchain.bat.example.
    exit /b 1
)
if not defined MEMESERVANT2_QT_ROOT (
    echo MEMESERVANT2_QT_ROOT is not set. See scripts\toolchain.bat.example.
    exit /b 1
)

call "%MEMESERVANT2_VSDEVCMD%" -arch=x64 -no_logo
if errorlevel 1 exit /b 1

"%MEMESERVANT2_CMAKE%" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MAKE_PROGRAM="%MEMESERVANT2_NINJA%" ^
  -DCMAKE_PREFIX_PATH="%MEMESERVANT2_QT_ROOT%"
if errorlevel 1 exit /b 1

"%MEMESERVANT2_NINJA%" -C build
