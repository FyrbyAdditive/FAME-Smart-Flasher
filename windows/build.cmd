@echo off
REM FAME Smart Flasher - Windows Build Script
REM Copyright 2025 Fyrby Additive Manufacturing & Engineering

echo.
echo Usage: build.cmd [options]
echo.
echo Options:
echo   debug      - Build debug configuration (default)
echo   release    - Build release configuration
echo   installer  - Build release and create installer
echo   clean      - Clean build artifacts
echo.

if "%1"=="" goto debug
if "%1"=="debug" goto debug
if "%1"=="release" goto release
if "%1"=="installer" goto installer
if "%1"=="clean" goto clean
goto usage

:debug
powershell -ExecutionPolicy Bypass -File "%~dp0build.ps1"
goto end

:release
powershell -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Release
goto end

:installer
powershell -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Release -Installer
goto end

:clean
powershell -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Clean
goto end

:usage
echo Unknown option: %1
echo.

:end
