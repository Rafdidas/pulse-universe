@echo off
setlocal EnableExtensions
rem Pulse Universe launcher.
rem   run.bat                 start the engine and open the browser (builds anything missing first)
rem   run.bat admin           run elevated: thread-to-core flows are measured instead of estimated
rem   run.bat rebuild         rebuild the frontend and the engine, then run
rem   run.bat admin rebuild   both
rem Press Ctrl+C in this window to stop.

cd /d "%~dp0"

set WANT_ADMIN=
set REBUILD=
:args
if "%~1"=="" goto args_done
if /i "%~1"=="admin" set WANT_ADMIN=1
if /i "%~1"=="rebuild" set REBUILD=1
shift
goto args
:args_done

if defined WANT_ADMIN (
    net session >nul 2>&1
    if errorlevel 1 (
        echo Restarting with administrator rights...
        set "FWD="
        if defined REBUILD set "FWD=rebuild"
        powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath cmd.exe -ArgumentList '/c ""%~f0"" admin %FWD%'"
        exit /b
    )
)

if not defined VCPKG_ROOT set "VCPKG_ROOT=C:/vcpkg"
set "PATH=C:\Program Files\CMake\bin;%PATH%"

set "ENGINE=engine\build\Release\pulse-engine.exe"
set "WEB=web\dist\index.html"

if defined REBUILD (
    del /q "%ENGINE%" >nul 2>&1
    del /q "%WEB%" >nul 2>&1
)

if not exist "%WEB%" (
    echo [1/2] Building the frontend...
    pushd web
    call npm install || goto fail
    call npm run build || goto fail
    popd
)

if not exist "%ENGINE%" (
    echo [2/2] Building the engine in Release. The first build takes a while...
    pushd engine
    if not exist build\CMakeCache.txt (
        cmake --preset default || goto fail
    )
    cmake --build build --config Release --target pulse-engine || goto fail
    popd
)

if not defined PULSE_NO_BROWSER (
    start "" /b powershell -NoProfile -Command "Start-Sleep -Seconds 2; Start-Process 'http://127.0.0.1:9000/'"
)

echo.
echo Pulse Universe: http://127.0.0.1:9000/   (Ctrl+C to stop)
echo.
"%ENGINE%" --serve --web-root web\dist
exit /b %ERRORLEVEL%

:fail
echo.
echo Build failed. See the error above.
pause
exit /b 1
