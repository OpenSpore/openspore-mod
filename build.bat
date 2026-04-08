@echo off
setlocal EnableDelayedExpansion

:: Spore is a 32-bit game - must compile x86
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1

set SDK_HEADERS=D:\dev\SporeModAPI-headers\Spore ModAPI
set SDK_SRC=D:\dev\SporeModAPI-headers\Spore ModAPI\SourceCode
set EASTL=D:\dev\SporeModAPI-headers\EASTL-3.02.01\include
set EABASE=D:\dev\SporeModAPI-headers\EASTL-3.02.01\test\packages\EABase\include\Common
set EASTDC=D:\dev\SporeModAPI-headers\EASTL-3.02.01\test\packages\EAStdC\include
set EAASSERT=D:\dev\SporeModAPI-headers\EASTL-3.02.01\test\packages\EAAssert\include
set EATHREAD=D:\dev\SporeModAPI-headers\EASTL-3.02.01\test\packages\EAThread\include
set DETOURS_INC=D:\dev\SporeModAPI-headers\Detours\include
set DETOURS_LIB=D:\dev\SporeModAPI-headers\Detours\lib.X86
set MOD_LIB=D:\dev\SporeModAPIdlls
set SRC=D:\dev\openspore-mod\.claude\worktrees\determined-sanderson\src
set VENDOR=D:\dev\openspore-mod\.claude\worktrees\determined-sanderson\vendor
set OUT=D:\dev\openspore-mod\.claude\worktrees\determined-sanderson\build

set NUGET_SDK=D:\dev\nuget_cache\packages\Microsoft.Windows.SDK.CPP.10.0.28000.1721\c
set WKITS_VER=10.0.28000.0
set WKITS_INC=%NUGET_SDK%\Include\%WKITS_VER%
set WKITS_LIB=%NUGET_SDK%\Lib\%WKITS_VER%

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\sdk" mkdir "%OUT%\sdk"

set INCLUDES=^
  /I"%SDK_HEADERS%" ^
  /I"%EASTL%" ^
  /I"%EABASE%" ^
  /I"%EASTDC%" ^
  /I"%EAASSERT%" ^
  /I"%EATHREAD%" ^
  /I"%DETOURS_INC%" ^
  /I"%VENDOR%" ^
  /I"%SRC%" ^
  /I"%WKITS_INC%\ucrt" ^
  /I"%WKITS_INC%\um" ^
  /I"%WKITS_INC%\shared"

set LIBPATHS=^
  /LIBPATH:"%MOD_LIB%" ^
  /LIBPATH:"%DETOURS_LIB%" ^
  /LIBPATH:"%WKITS_LIB%\um\x86" ^
  /LIBPATH:"%WKITS_LIB%\ucrt\x86"

set CFLAGS=/nologo /EHsc /std:c++17 /DUNICODE /D_UNICODE /D_WINDLL /MD /W0 /Zi

echo [1/3] Compiling SporeModAPI SDK sources...
for /r "%SDK_SRC%" %%f in (*.cpp) do (
  set FNAME=%%~nf
  cl.exe %CFLAGS% %INCLUDES% /Fo"%OUT%\sdk\%%~nf.obj" /c "%%f" 2>&1
  if errorlevel 1 (
    echo FAILED: %%f
    goto :error
  )
)

echo.
echo [2/3] Compiling mod sources...
cl.exe %CFLAGS% %INCLUDES% /Fo"%OUT%\\" /c ^
  "%SRC%\dllmain.cpp" ^
  "%SRC%\OpenSporeMod.cpp" ^
  "%SRC%\net\HttpClient.cpp" ^
  "%SRC%\net\ApiClient.cpp" ^
  "%SRC%\net\SseClient.cpp" ^
  "%SRC%\ui\NetworkMenu.cpp" ^
  "%SRC%\ui\GalaxyOverlay.cpp" ^
  2>&1
if errorlevel 1 goto :error

echo.
echo [3/3] Linking DLL (x86)...
link.exe /nologo /DLL /OUT:"%OUT%\OpenSporeMod.dll" /PDB:"%OUT%\OpenSporeMod.pdb" ^
  %LIBPATHS% ^
  SporeModAPI.lib detours.lib winhttp.lib shlwapi.lib shell32.lib user32.lib ^
  "%OUT%\dllmain.obj" ^
  "%OUT%\OpenSporeMod.obj" ^
  "%OUT%\HttpClient.obj" ^
  "%OUT%\ApiClient.obj" ^
  "%OUT%\SseClient.obj" ^
  "%OUT%\NetworkMenu.obj" ^
  "%OUT%\GalaxyOverlay.obj" ^
  "%OUT%\sdk\*.obj" ^
  2>&1
if errorlevel 1 goto :error

echo.
echo =============================================
echo  BUILD OK: %OUT%\OpenSporeMod.dll
echo =============================================
goto :end

:error
echo.
echo BUILD FAILED
exit /b 1

:end
endlocal
