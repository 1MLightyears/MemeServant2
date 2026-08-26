@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
if errorlevel 1 exit /b 1

"D:\Program Files\CMake\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MAKE_PROGRAM="D:\Qt\Tools\Ninja\ninja.exe" ^
  -DCMAKE_PREFIX_PATH="D:\Qt\6.11.2\msvc2022_64"
if errorlevel 1 exit /b 1

"D:\Qt\Tools\Ninja\ninja.exe" -C build
