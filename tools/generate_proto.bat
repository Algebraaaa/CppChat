@echo off
setlocal
rem Use the same vcpkg tools as the local server build.
set "PROJECT_ROOT=%~dp0.."
if not defined VCPKG_ROOT set "VCPKG_ROOT=D:\Dev\vcpkg"
if not defined VCPKG_TARGET_TRIPLET set "VCPKG_TARGET_TRIPLET=x64-windows-static-md-release"
if not defined PROTOC set "PROTOC=%VCPKG_ROOT%\installed\%VCPKG_TARGET_TRIPLET%\tools\protobuf\protoc.exe"
if not defined GRPC_CPP_PLUGIN set "GRPC_CPP_PLUGIN=%VCPKG_ROOT%\installed\%VCPKG_TARGET_TRIPLET%\tools\grpc\grpc_cpp_plugin.exe"

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=All"
if /I "%TARGET%"=="--no-pause" set "TARGET=All"
if /I "%TARGET%"=="--help" goto help
if /I "%TARGET%"=="All" goto tools
if /I "%TARGET%"=="GateServer" goto tools
if /I "%TARGET%"=="StatusServer" goto tools
if /I "%TARGET%"=="ChatServerShared" goto tools
echo [ERROR] Unknown target: %TARGET%
exit /b 1

:tools
if not exist "%PROTOC%" (
    echo [ERROR] protoc.exe not found: "%PROTOC%"
    exit /b 1
)
if not exist "%GRPC_CPP_PLUGIN%" (
    echo [ERROR] grpc_cpp_plugin.exe not found: "%GRPC_CPP_PLUGIN%"
    exit /b 1
)
if /I not "%TARGET%"=="All" goto single
for %%D in (GateServer StatusServer ChatServerShared) do (
    call :generate "%%D"
    if errorlevel 1 exit /b 1
)
goto success

:single
call :generate "%TARGET%"
if errorlevel 1 exit /b 1

:success
echo [SUCCESS] C++ Protobuf and gRPC source files generated.
exit /b 0

:generate
set "PROTO_DIR=%PROJECT_ROOT%\%~1"
if not exist "%PROTO_DIR%\message.proto" (
    echo [ERROR] Missing protocol: "%PROTO_DIR%\message.proto"
    exit /b 1
)
echo Generating %~1/message.proto...
"%PROTOC%" "--proto_path=%PROTO_DIR%" "--cpp_out=%PROTO_DIR%" "--grpc_out=%PROTO_DIR%" "--plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN%" "%PROTO_DIR%\message.proto"
if errorlevel 1 exit /b 1
for %%F in (message.pb.h message.pb.cc message.grpc.pb.h message.grpc.pb.cc) do (
    if not exist "%PROTO_DIR%\%%F" exit /b 1
)
exit /b 0

:help
echo Usage: tools\generate_proto.bat [All^|GateServer^|StatusServer^|ChatServerShared]
echo Generates message.pb.* and message.grpc.pb.* beside each message.proto.
echo ChatServer and ChatServer2 share ChatServerShared/message.proto.
echo VerifyServer loads its protocol at runtime and needs no C++ generation.
echo Override PROTOC, GRPC_CPP_PLUGIN, VCPKG_ROOT or VCPKG_TARGET_TRIPLET if needed.
echo CMake builds also generate their own protocol files in the build directory.
exit /b 0
