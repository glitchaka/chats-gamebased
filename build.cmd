@echo off
setlocal

where cmake >nul 2>nul || (
  echo CMake no esta instalado o no esta en PATH.
  exit /b 1
)

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || exit /b 1
cmake --build build --config Release --parallel || exit /b 1

echo.
echo Ejecutable: build\bin\Release\chats-gamebased.exe
