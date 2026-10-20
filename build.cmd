@echo off
setlocal EnableExtensions

where cmake >nul 2>nul || (
  echo ERROR: CMake no esta instalado o no esta en PATH.
  exit /b 1
)

set "BUILD_DIR=build"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="

rem If cl.exe is already available, use that environment as-is.
where cl >nul 2>nul && goto :msvc_ready

rem Detect any installed Visual Studio / Build Tools version that actually has the C++ toolchain.
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
)

if defined VSROOT (
  if exist "%VSROOT%\Common7\Tools\VsDevCmd.bat" (
    call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
  )
)

:msvc_ready
where cl >nul 2>nul && goto :configure_msvc

rem Fallback for MinGW if the user has GCC instead of MSVC.
where g++ >nul 2>nul && goto :configure_mingw

echo ERROR: No se encontro un compilador C++ de Windows.
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%I"
  if defined VSROOT (
    echo Visual Studio esta instalado en:
    echo   %VSROOT%
    echo pero no aparece el componente C++ x64 ^(MSVC^).
    echo Instala el workload "Desktop development with C++" o los Build Tools C++.
  ) else (
    echo Visual Studio Installer no reporta ninguna instancia instalada.
  )
)
exit /b 1

:clean_cache
if exist "%BUILD_DIR%\CMakeCache.txt" (
  rmdir /s /q "%BUILD_DIR%"
)
exit /b 0

:configure_msvc
call :clean_cache
where ninja >nul 2>nul
if not errorlevel 1 (
  echo Usando MSVC + Ninja.
  cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
) else (
  echo Usando MSVC + NMake.
  cmake -S . -B "%BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
goto :build

:configure_mingw
call :clean_cache
where ninja >nul 2>nul
if not errorlevel 1 (
  echo Usando MinGW GCC + Ninja.
  cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
) else (
  echo Usando MinGW Makefiles.
  cmake -S . -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release || exit /b 1
)

:build
cmake --build "%BUILD_DIR%" --parallel || exit /b 1

if exist "%BUILD_DIR%\bin\chats-gamebased.exe" (
  echo.
  echo Ejecutable: %BUILD_DIR%\bin\chats-gamebased.exe
  exit /b 0
)
if exist "%BUILD_DIR%\bin\Release\chats-gamebased.exe" (
  echo.
  echo Ejecutable: %BUILD_DIR%\bin\Release\chats-gamebased.exe
  exit /b 0
)

echo ERROR: La compilacion termino pero no se encontro chats-gamebased.exe.
exit /b 1
