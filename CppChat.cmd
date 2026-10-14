@echo off
setlocal
rem Use Windows PowerShell's modules even when launched from PowerShell 7.
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules;%ProgramFiles%\WindowsPowerShell\Modules"
if not "%~1"=="" goto command
setlocal EnableDelayedExpansion

:menu
set "BUILD_TARGET=All"
echo.
echo CppChat
echo  1. Start
echo  2. Stop
echo  3. Debug servers and client (VS + Qt Creator)
echo  4. Build Release
echo  5. Build Debug
echo  6. Debug client (Qt Creator)
echo  7. Build one server (Release)
echo  8. Build one server (Debug)
echo  9. Generate Protobuf and gRPC sources
echo 10. Build Windows installer
echo  0. Exit
set "SELECTION="
set /p "SELECTION=Select (press Enter): "
if "!SELECTION!"=="0" exit /b 0
if "!SELECTION!"=="1" goto start
if "!SELECTION!"=="2" goto stop
if "!SELECTION!"=="3" goto debug
if "!SELECTION!"=="4" goto buildrelease
if "!SELECTION!"=="5" goto builddebug
if "!SELECTION!"=="6" goto clientdebug
if "!SELECTION!"=="7" goto buildsinglerelease
if "!SELECTION!"=="8" goto buildsingledebug
if "!SELECTION!"=="9" goto selectprotocoltarget
if "!SELECTION!"=="10" goto package
echo Please enter a number from 0 to 10.
goto menu

:start
set "ACTION=start"
set "CONFIGURATION=Release"
goto run

:package
set "ACTION=package"
set "CONFIGURATION=Release"
goto run

:stop
set "ACTION=stop"
set "CONFIGURATION=Release"
goto run

:debug
set "ACTION=debug"
set "CONFIGURATION=Debug"
goto run

:buildrelease
set "ACTION=build"
set "CONFIGURATION=Release"
goto run

:builddebug
set "ACTION=build"
set "CONFIGURATION=Debug"
goto run

:clientdebug
set "ACTION=debug-client"
set "CONFIGURATION=Debug"
goto run

:buildsinglerelease
set "ACTION=build"
set "CONFIGURATION=Release"
goto selectbuildtarget

:buildsingledebug
set "ACTION=build"
set "CONFIGURATION=Debug"

:selectbuildtarget
echo.
echo Build one server - %CONFIGURATION%
echo  1. GateServer
echo  2. StatusServer
echo  3. ChatServer
echo  4. ChatServer2
echo  5. ResourceServer
echo  0. Back
set "SELECTION="
set "BUILD_TARGET="
set /p "SELECTION=Select (press Enter): "
if "!SELECTION!"=="0" goto menu
if "!SELECTION!"=="1" set "BUILD_TARGET=GateServer"
if "!SELECTION!"=="2" set "BUILD_TARGET=StatusServer"
if "!SELECTION!"=="3" set "BUILD_TARGET=ChatServer"
if "!SELECTION!"=="4" set "BUILD_TARGET=ChatServer2"
if "!SELECTION!"=="5" set "BUILD_TARGET=ResourceServer"
if defined BUILD_TARGET goto run
echo Please enter a number from 0 to 5.
goto selectbuildtarget

:selectprotocoltarget
set "ACTION=generate-proto"
echo.
echo Generate Protobuf and gRPC sources
echo  1. All
echo  2. GateServer
echo  3. StatusServer
echo  4. ChatServerShared (both chat nodes)
echo  0. Back
set "SELECTION="
set "PROTOCOL_TARGET="
set /p "SELECTION=Select (press Enter): "
if "!SELECTION!"=="0" goto menu
if "!SELECTION!"=="1" set "PROTOCOL_TARGET=All"
if "!SELECTION!"=="2" set "PROTOCOL_TARGET=GateServer"
if "!SELECTION!"=="3" set "PROTOCOL_TARGET=StatusServer"
if "!SELECTION!"=="4" set "PROTOCOL_TARGET=ChatServerShared"
if defined PROTOCOL_TARGET goto run
echo Please enter a number from 0 to 4.
goto selectprotocoltarget

:run
if "%ACTION%"=="generate-proto" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0.runtime\cppchat.ps1" -Action generate-proto -ProtocolTarget "%PROTOCOL_TARGET%"
) else if "%ACTION%"=="build" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0.runtime\cppchat.ps1" -Action build -Configuration "%CONFIGURATION%" -Target "%BUILD_TARGET%"
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0.runtime\cppchat.ps1" -Action "%ACTION%" -Configuration "%CONFIGURATION%"
)
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" echo Operation failed (exit code %RESULT%).
goto menu

:command
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0.runtime\cppchat.ps1" %*
exit /b %ERRORLEVEL%
