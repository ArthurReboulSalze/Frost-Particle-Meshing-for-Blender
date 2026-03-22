@echo off
setlocal enabledelayedexpansion

:: Create logs directory if it doesn't exist
if not exist "logs" mkdir "logs"

:: Generate timestamp for unique log filename
for /f "tokens=2 delims==" %%I in ('wmic os get localdatetime /value') do set datetime=%%I
set "TIMESTAMP=%datetime:~0,4%-%datetime:~4,2%-%datetime:~6,2%_%datetime:~8,2%-%datetime:~10,2%"
set "LOGFILE=logs\build_%TIMESTAMP%.log"

echo ==================================================
echo  FROST BUILD WRAPPER
echo  Redirecting output to: %LOGFILE%
echo ==================================================

:: Run the build script and redirect both stdout and stderr
echo [INFO] Starting build_nopause.bat...
call build_nopause.bat > "%LOGFILE%" 2>&1

:: Check exit code
if errorlevel 1 (
    echo [ERROR] Build FAILED!
    echo.
    echo Tail of the log:
    echo --------------------------------------------------
    powershell -Command "Get-Content '%LOGFILE%' -Tail 20"
    echo --------------------------------------------------
    echo Please check %LOGFILE% for full details.
    exit /b 1
) else (
    echo [SUCCESS] Build completed successfully.
    echo.
    echo Build summary:
    powershell -Command "Get-Content '%LOGFILE%' | Select-String 'Build Complete' -Context 0,2"
)

endlocal
