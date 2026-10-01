@echo off
rem Build the lab DLL, injector and netlab with MSVC (x86). Run from a "x86 Native Tools" prompt
rem or let this script call vcvarsall. Output: build-win32\
setlocal
set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
if exist "%VS%\VC\Auxiliary\Build\vcvarsall.bat" call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
cd /d "%~dp0..\.."
if not exist build-win32 mkdir build-win32
cl /nologo /LD /EHsc /O2 /std:c++17 /W3 /D_CRT_SECURE_NO_WARNINGS /Isrc /Fo:build-win32\ /Fe:build-win32\osmp_lab.dll ^
   src\lab\LabMain.cpp src\lab\RawEngine.cpp src\game\Replication.cpp src\game\GhostLab.cpp ^
   src\net\Protocol.cpp src\net\UdpSocket.cpp src\net\Session.cpp src\net\Interpolation.cpp ws2_32.lib
if errorlevel 1 exit /b 1
cl /nologo /EHsc /O2 /std:c++17 /D_CRT_SECURE_NO_WARNINGS /Fo:build-win32\ /Fe:build-win32\inject.exe src\lab\inject.cpp
cl /nologo /EHsc /O2 /std:c++17 /D_CRT_SECURE_NO_WARNINGS /Isrc /Fo:build-win32\ /Fe:build-win32\osmp_netlab.exe ^
   tools\netlab.cpp src\net\Protocol.cpp src\net\UdpSocket.cpp src\net\Session.cpp src\net\Interpolation.cpp ws2_32.lib
echo [+] built into build-win32\
endlocal
