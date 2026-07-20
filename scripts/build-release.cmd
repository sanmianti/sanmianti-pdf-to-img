@echo off
setlocal
set "CMAKE_EXE=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"
"%CMAKE_EXE%" -S "%~dp0.." -B "%~dp0..\build" -A Win32 -T v142 || exit /b 1
"%CMAKE_EXE%" --build "%~dp0..\build" --config Release || exit /b 1
"%CMAKE_EXE%" --build "%~dp0..\build" --config Release --target RUN_TESTS || exit /b 1
endlocal

