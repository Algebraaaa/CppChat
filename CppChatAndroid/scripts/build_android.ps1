param(
    [Parameter(Mandatory)][string]$BackendRoot,
    [Parameter(Mandatory)][string]$QtAndroidRoot,
    [Parameter(Mandatory)][string]$QtHostRoot,
    [Parameter(Mandatory)][string]$SdkRoot,
    [Parameter(Mandatory)][string]$NdkRoot,
    [Parameter(Mandatory)][string]$OpenSslRoot,
    [Parameter(Mandatory)][string]$JavaRoot,
    [Parameter(Mandatory)][string]$Ninja,
    [string]$CMake = 'cmake',
    [string]$ConfigFile = '',
    [string]$GradleCache = '',
    [string]$ReleaseNotes = '安卓语音消息、语音/视频通话和文件收发；断线自动恢复，保留本地记录与草稿。',
    [int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ConfigFile) { $ConfigFile = Join-Path $projectRoot 'android/config.ini' }
if (-not (Test-Path -LiteralPath $ConfigFile -PathType Leaf)) {
    throw 'Create a private android/config.ini from config.example.ini, or pass -ConfigFile. Deployment configuration must not be committed.'
}
$ConfigFile = (Resolve-Path -LiteralPath $ConfigFile).Path
$projectVersion = [regex]::Match((Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw), 'project\(CppChatAndroid VERSION ([0-9.]+)').Groups[1].Value
if (-not $projectVersion) { throw 'Cannot determine Android project version' }
$buildRoot = Join-Path $projectRoot 'build/android-arm64'
$report = Join-Path $projectRoot ('build/regression/android-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$release = Join-Path $projectRoot 'build/release'
New-Item -ItemType Directory -Force $buildRoot,$report,$release | Out-Null
foreach ($library in @('libssl_3.so','libcrypto_3.so')) {
    if (-not (Test-Path -LiteralPath (Join-Path $OpenSslRoot $library))) {
        throw "Build the supported OpenSSL runtime first: $library"
    }
}
$env:JAVA_HOME = $JavaRoot
$env:ANDROID_SDK_ROOT = $SdkRoot
$env:ANDROID_NDK_ROOT = $NdkRoot
$env:GRADLE_USER_HOME = if ($GradleCache) { $GradleCache } else { Join-Path $projectRoot 'build/gradle-cache' }
$env:GRADLE_OPTS = '-Dorg.gradle.daemon=false'
$env:Path = "$QtHostRoot/bin;$JavaRoot/bin;$(Split-Path $Ninja);$env:Path"

function Invoke-Checked([string]$Program, [string[]]$Arguments, [string]$Log) {
    & $Program @Arguments *> $Log
    if ($LASTEXITCODE -ne 0) { throw "Command failed; retained evidence: $Log" }
}

Invoke-Checked $CMake @('-S',$projectRoot,'-B',$buildRoot,'-G','Ninja',
    "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DCMAKE_TOOLCHAIN_FILE=$QtAndroidRoot/lib/cmake/Qt6/qt.toolchain.cmake",
    "-DQT_HOST_PATH=$QtHostRoot", "-DANDROID_SDK_ROOT=$SdkRoot", "-DANDROID_NDK_ROOT=$NdkRoot",
    '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-28','-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON',
    "-DCPPCHAT_BACKEND_ROOT=$BackendRoot", "-DCPPCHAT_ANDROID_CONFIG_FILE=$ConfigFile",
    "-DANDROID_OPENSSL_ROOT=$OpenSslRoot",'-DCMAKE_BUILD_TYPE=Release') (Join-Path $report 'configure.log')
Invoke-Checked $CMake @('--build',$buildRoot,'-j',"$Jobs") (Join-Path $report 'build.log')
$unsigned = Join-Path $buildRoot 'android-build/build/outputs/apk/release/android-build-release-unsigned.apk'
if (-not (Test-Path -LiteralPath $unsigned)) { throw 'Unsigned Release APK was not produced' }
$signing = Join-Path $projectRoot 'build/signing'
New-Item -ItemType Directory -Force $signing | Out-Null
$key = Join-Path $signing 'cppchat-android-test.jks'
if (-not (Test-Path -LiteralPath $key)) {
    Invoke-Checked "$JavaRoot/bin/keytool.exe" @('-genkeypair','-keystore',$key,'-storepass','android',
        '-keypass','android','-alias','cppchat-android-test','-keyalg','RSA','-keysize','3072',
        '-validity','3650','-dname','CN=CppChat Android Test,O=Local Development,C=CN','-noprompt') (Join-Path $report 'test-key.log')
}
$apk = Join-Path $release "CppChat-$projectVersion-android-arm64.apk"
$tools = Join-Path $SdkRoot 'build-tools/36.0.0'
Invoke-Checked "$tools/apksigner.bat" @('sign','--ks',$key,'--ks-key-alias','cppchat-android-test',
    '--ks-pass','pass:android','--key-pass','pass:android','--out',$apk,$unsigned) (Join-Path $report 'sign.log')
Invoke-Checked "$tools/apksigner.bat" @('verify','--verbose','--print-certs',$apk) (Join-Path $report 'signature.txt')
Invoke-Checked "$tools/zipalign.exe" @('-c','-P','16','-v','4',$apk) (Join-Path $report 'alignment.txt')
Invoke-Checked "$tools/aapt2.exe" @('dump','badging',$apk) (Join-Path $report 'manifest.txt')
Invoke-Checked 'python' @('-B',"$projectRoot/tests/audit_android_apk.py",$apk,'--output',"$report/apk-audit.json",'--badging',"$report/manifest.txt",'--media') (Join-Path $report 'audit.log')
$hash = (Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($apk + '.sha256', "$hash  $([IO.Path]::GetFileName($apk))`n")
$updateManifest = [ordered]@{
    version = $projectVersion
    notes = $ReleaseNotes
    downloadUrl = './' + [IO.Path]::GetFileName($apk)
    size = (Get-Item -LiteralPath $apk).Length
    sha256 = $hash
}
$updateManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $release "CppChat-Update-$projectVersion.json") -Encoding utf8
Write-Output "Test-signed ARM64 APK: $apk"
Write-Output "SHA256: $hash"
Write-Output "Evidence: $report"
