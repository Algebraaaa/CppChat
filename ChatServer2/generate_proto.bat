@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PAUSE_AT_END=1"

if /I "%~1"=="--no-pause" set "PAUSE_AT_END="

if not defined VCPKG_ROOT (
    echo [ERROR] VCPKG_ROOT is not set.
    echo         Set it to your vcpkg root directory and try again.
    goto :failed
)

set "PROTOC=%VCPKG_ROOT%\installed\x64-windows\tools\protobuf\protoc.exe"
set "GRPC_CPP_PLUGIN=%VCPKG_ROOT%\installed\x64-windows\tools\grpc\grpc_cpp_plugin.exe"

if not exist "%PROTOC%" (
    echo [ERROR] protoc.exe was not found:
    echo         %PROTOC%
    goto :failed
)

if not exist "%GRPC_CPP_PLUGIN%" (
    echo [ERROR] grpc_cpp_plugin.exe was not found:
    echo         %GRPC_CPP_PLUGIN%
    goto :failed
)

if not exist "%SCRIPT_DIR%message.proto" (
    echo [ERROR] message.proto was not found beside this script.
    goto :failed
)

pushd "%SCRIPT_DIR%" || goto :failed

echo Generating C++ Protobuf and gRPC files from message.proto...
"%PROTOC%" ^
    --proto_path=. ^
    --cpp_out=. ^
    --grpc_out=. ^
    "--plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN%" ^
    message.proto

if errorlevel 1 (
    set "GENERATION_EXIT_CODE=%ERRORLEVEL%"
    popd
    goto :generation_failed
)

for %%F in (message.pb.h message.pb.cc message.grpc.pb.h message.grpc.pb.cc) do (
    if not exist "%%F" (
        echo [ERROR] Expected output was not generated: %%F
        popd
        goto :failed
    )
)

popd
echo.
echo [SUCCESS] Generated files:
echo           message.pb.h
echo           message.pb.cc
echo           message.grpc.pb.h
echo           message.grpc.pb.cc
echo.
if defined PAUSE_AT_END pause
exit /b 0

:generation_failed
echo.
echo [ERROR] Protobuf generation failed with exit code %GENERATION_EXIT_CODE%.
echo         Check the message.proto syntax, tool paths, and file write permissions.
if defined PAUSE_AT_END pause
exit /b %GENERATION_EXIT_CODE%

:failed
echo.
if defined PAUSE_AT_END pause
exit /b 1
