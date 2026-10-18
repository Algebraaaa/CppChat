param(
  [ValidatePattern('^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$')][string]$Version = '0.1.12',
  [ValidateLength(0,8000)][string]$ReleaseNotes = '',
  [string]$SigningConfig,
  [switch]$RequireSigning,
  [string]$BuildDirectory,
  [string]$ConfigFile,
  [string]$IsccPath,
  [string]$QtLicenseDirectory,
  [ValidateRange(1,64)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$clientRoot = Split-Path $PSScriptRoot -Parent
$repositoryRoot = Split-Path $clientRoot -Parent
. (Join-Path $PSScriptRoot 'release-history.ps1')
$releaseInfo = Get-CppChatReleaseInfo -HistoryFile (Join-Path $clientRoot 'Resources\release-history.json') -Version $Version -CurrentNotes $ReleaseNotes
$ReleaseNotes = $releaseInfo.notes
if (-not $SigningConfig -and (Test-Path (Join-Path $repositoryRoot '.runtime\code-signing.json'))) {
  $SigningConfig = Join-Path $repositoryRoot '.runtime\code-signing.json'
}
if ($RequireSigning -and -not $SigningConfig) { throw 'Code signing is required but not configured.' }
if ($SigningConfig) { $SigningConfig = (Resolve-Path -LiteralPath $SigningConfig).Path }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $clientRoot 'build\release' }
if (-not $ConfigFile) { $ConfigFile = Join-Path $clientRoot 'config.ini' }
$cacheFile = Join-Path $BuildDirectory 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cacheFile)) { throw 'Configure the client Release build first.' }
$cache = Get-Content -LiteralPath $cacheFile
function Get-BuildSetting([string]$Name) {
  $pattern = '^' + [regex]::Escape($Name) + ':[^=]*=(.*)$'
  foreach ($line in $cache) { if ($line -match $pattern) { return $Matches[1] } }
  throw "Missing CMake setting: $Name"
}
if ((Get-BuildSetting 'CMAKE_BUILD_TYPE') -ne 'Release') { throw 'Installer requires a Release build.' }
$cmake = Get-BuildSetting 'CMAKE_COMMAND'
$qtRoot = [IO.Path]::GetFullPath((Join-Path (Get-BuildSetting 'Qt6_DIR') '..\..\..'))
$compilerBin = Split-Path (Get-BuildSetting 'CMAKE_CXX_COMPILER') -Parent
$compilerRoot = Split-Path $compilerBin -Parent
$deployQt = Join-Path $qtRoot 'bin\windeployqt.exe'
if (-not $IsccPath) {
  $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
  if ($command) { $IsccPath = $command.Source }
  foreach ($hive in @('HKCU','HKLM')) {
    if ($IsccPath) { break }
    $key = "${hive}:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1"
    $settings = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
    if ($settings.InstallLocation) { $IsccPath = Join-Path $settings.InstallLocation 'ISCC.exe' }
  }
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath)) {
  throw 'Install Inno Setup 6.7.3 or pass -IsccPath <ISCC.exe>.'
}
if (-not (Test-Path -LiteralPath $deployQt)) { throw 'windeployqt is missing from the selected Qt Kit.' }

$publicConfig = @{}
$section = ''
foreach ($line in (Get-Content -LiteralPath $ConfigFile)) {
  if ($line -match '^\s*\[([^\]]+)\]\s*$') { $section = $Matches[1] }
  elseif ($line -match '^\s*([^;#=]+?)\s*=\s*(.*?)\s*$') {
    $publicConfig["$section/$($Matches[1])"] = $Matches[2]
  }
}
foreach ($key in @('GateServer/host','GateServer/port','GateServer/scheme','ChatServer/tls',
                   'ResourceServer/host','ResourceServer/port','ResourceServer/tls')) {
  if (-not $publicConfig[$key]) { throw "Missing public client configuration: $key" }
}
foreach ($key in @('GateServer/host','ResourceServer/host')) {
  if ($publicConfig[$key] -match '^(localhost|127\.|0\.0\.0\.0$|::1$)') {
    throw 'The release configuration must point to the public server.'
  }
}
if ($publicConfig['GateServer/scheme'] -ne 'https' -or
    $publicConfig['ChatServer/tls'] -ne 'true' -or $publicConfig['ResourceServer/tls'] -ne 'true') {
  throw 'Release packages require HTTPS and TLS for every public connection.'
}
$configText = @"
[GateServer]
host=$($publicConfig['GateServer/host'])
port=$($publicConfig['GateServer/port'])
scheme=$($publicConfig['GateServer/scheme'])

[ChatServer]
tls=$($publicConfig['ChatServer/tls'])

[ResourceServer]
host=$($publicConfig['ResourceServer/host'])
port=$($publicConfig['ResourceServer/port'])
tls=$($publicConfig['ResourceServer/tls'])
"@
$packageRoot = Join-Path $repositoryRoot 'build\packages'
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$workRoot = Join-Path $repositoryRoot "build\packaging\$Version\$runId"
$stage = Join-Path $workRoot 'stage'
New-Item -ItemType Directory -Path $stage,$packageRoot -Force | Out-Null
function Invoke-PackagingTool([string]$Tool, [string[]]$Arguments, [string]$LogName) {
  $log = Join-Path $workRoot $LogName
  $previousPreference = $ErrorActionPreference
  try {
    # Windows PowerShell treats native stderr warnings as ErrorRecord objects.
    # Capture them in the log and use the native exit code to decide success.
    $ErrorActionPreference = 'Continue'
    & $Tool @Arguments *> $log
    $toolExitCode = $LASTEXITCODE
  } finally { $ErrorActionPreference = $previousPreference }
  if ($toolExitCode -ne 0) { throw "Tool failed (exit $toolExitCode): $log" }
}
$oldPath = $env:PATH
$packageSucceeded = $false
try {
  $env:PATH = "$compilerBin;$(Join-Path $qtRoot 'bin');" + $env:PATH
  Invoke-PackagingTool $cmake @('-S',$clientRoot,'-B',$BuildDirectory,"-DCPPCHAT_APP_VERSION=$Version") 'configure.log'
  Invoke-PackagingTool $cmake @('--build',$BuildDirectory,'--target','CppChat','--parallel',"$Jobs") 'build.log'
  Copy-Item -LiteralPath (Join-Path $BuildDirectory 'CppChat.exe') -Destination $stage
  $strip = Join-Path $compilerBin 'strip.exe'
  if (Test-Path -LiteralPath $strip) {
    Invoke-PackagingTool $strip @('--strip-debug',(Join-Path $stage 'CppChat.exe')) 'strip.log'
  }
  Invoke-PackagingTool $deployQt @('--release','--compiler-runtime','--no-translations',
    '--no-system-d3d-compiler','--no-system-dxc-compiler','--no-opengl-sw','--skip-plugin-types','sqldrivers',
    (Join-Path $stage 'CppChat.exe')) 'windeployqt.log'
  New-Item -ItemType Directory -Path (Join-Path $stage 'sqldrivers') -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $qtRoot 'plugins\sqldrivers\qsqlite.dll') -Destination (Join-Path $stage 'sqldrivers')
  Copy-Item -LiteralPath (Join-Path $BuildDirectory 'sqldrivers\CppChatSqliteCipher.dll') -Destination (Join-Path $stage 'sqldrivers')
  if ($SigningConfig) {
    & (Join-Path $PSScriptRoot 'sign-artifact.ps1') -Artifact (Join-Path $stage 'CppChat.exe') -SigningConfig $SigningConfig *> (Join-Path $workRoot 'sign-client.log')
    & (Join-Path $PSScriptRoot 'sign-artifact.ps1') -Artifact (Join-Path $stage 'sqldrivers\CppChatSqliteCipher.dll') -SigningConfig $SigningConfig *> (Join-Path $workRoot 'sign-cipher.log')
  }
  [IO.File]::WriteAllText((Join-Path $stage 'config.ini'),$configText,[Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText((Join-Path $stage 'qt.conf'),"[Paths]`nPrefix=.`nPlugins=.`n",[Text.UTF8Encoding]::new($false))

  $licenses = Join-Path $stage 'licenses'
  New-Item -ItemType Directory -Path $licenses -Force | Out-Null
  $opusSource = Get-BuildSetting 'CPPCHAT_OPUS_SOURCE'
  Copy-Item -LiteralPath (Join-Path $opusSource 'COPYING') -Destination (Join-Path $licenses 'Opus-BSD.txt')
  $cipherSources = Join-Path $licenses 'Encrypted-SQLite-Source'
  New-Item -ItemType Directory -Path $cipherSources | Out-Null
  $driverSource = Get-BuildSetting 'CPPCHAT_MC_DRIVER_SOURCE'
  $engineSource = Get-BuildSetting 'CPPCHAT_MC_SOURCE'
  foreach ($file in @('qsql_sqlite.cpp','qsql_sqlite_p.h','qsql_sqlite_vfs.cpp','qsql_sqlite_vfs_p.h',
      'qsql_sqlite_multiple_ciphers.cpp','qsql_sqlite_multiple_ciphers_p.h','smain.cpp','QSQLite3MultipleCiphers.json')) {
    Copy-Item -LiteralPath (Join-Path $driverSource $file) -Destination $cipherSources
  }
  Copy-Item -LiteralPath (Join-Path $engineSource 'sqlite3mc_amalgamation.c'),(Join-Path $engineSource 'sqlite3mc_amalgamation.h'),(Join-Path $engineSource 'sqlite3.h') -Destination $cipherSources
  Copy-Item -LiteralPath (Join-Path $clientRoot 'cmake\EncryptedSqlite.cmake') -Destination $cipherSources
  Copy-Item -LiteralPath (Join-Path (Split-Path $driverSource -Parent) 'README.md') -Destination (Join-Path $cipherSources 'Qt-driver-README.md')
  Copy-Item -LiteralPath (Join-Path $clientRoot 'packaging\encrypted-sqlite-notices.txt') -Destination $cipherSources
  Copy-Item -LiteralPath (Join-Path $repositoryRoot 'LICENSE') -Destination (Join-Path $licenses 'CppChat-MIT.txt')
  Copy-Item -LiteralPath (Join-Path $clientRoot 'third_party\spdlog\LICENSE') -Destination (Join-Path $licenses 'spdlog-MIT.txt')
  Copy-Item -LiteralPath (Join-Path $clientRoot 'third_party\pinyin-data\LICENSE') -Destination (Join-Path $licenses 'pinyin-data-MIT.txt')
  Copy-Item -LiteralPath (Join-Path $clientRoot 'Resources\icons\LICENSE.txt') -Destination (Join-Path $licenses 'Lucide-ISC.txt')
  foreach ($file in @('OFL.txt','SourceHanSans-LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $clientRoot "Resources\fonts\$file") -Destination $licenses
  }
  $qtLicenses = if ($QtLicenseDirectory) { (Resolve-Path -LiteralPath $QtLicenseDirectory).Path } else { [IO.Path]::GetFullPath((Join-Path $qtRoot '..\..\Licenses')) }
  Copy-Item -LiteralPath $qtLicenses -Destination (Join-Path $licenses 'Qt') -Recurse
  foreach ($component in @('gcc','mingw-w64','winpthreads')) {
    Copy-Item -LiteralPath (Join-Path $compilerRoot "licenses\$component") -Destination (Join-Path $licenses "MinGW-$component") -Recurse
  }
  Copy-Item -LiteralPath (Join-Path (Split-Path $IsccPath -Parent) 'license.txt') -Destination (Join-Path $licenses 'Inno-Setup.txt')
  New-Item -ItemType Directory -Path (Join-Path $licenses 'Qt-SBOM') -Force | Out-Null
  foreach ($module in @('qtbase','qtsvg','qtmultimedia')) {
    Get-ChildItem -LiteralPath (Join-Path $qtRoot 'sbom') -Filter "$module-*.spdx" |
      Copy-Item -Destination (Join-Path $licenses 'Qt-SBOM')
  }
  $notice = "CppChat $Version`nQt runtime deployed from the selected Kit using windeployqt.`nQt sources: https://code.qt.io/qt/qtbase.git/, https://code.qt.io/qt/qtsvg.git/ and https://code.qt.io/qt/qtmultimedia.git/ (exact build revisions are in Qt-SBOM).`nQt modules are dynamically linked; the DLLs can be replaced with compatible builds.`nOpus 1.5.2: https://opus-codec.org/release/stable/2024/04/12/libopus-1_5_2.html (unmodified BSD codec, COPYING included).`nInstaller: Inno Setup 6.7.3, https://jrsoftware.org/ (license included).`nFonts and UI icon licenses are included.`n"
  [IO.File]::WriteAllText((Join-Path $licenses 'THIRD-PARTY-NOTICES.txt'),$notice,[Text.UTF8Encoding]::new($false))
  foreach ($file in @('CppChat.exe','Qt6Core.dll','Qt6Widgets.dll','Qt6Multimedia.dll','imageformats\qjpeg.dll','licenses\Opus-BSD.txt','platforms\qwindows.dll',
                      'sqldrivers\qsqlite.dll','sqldrivers\CppChatSqliteCipher.dll','tls\qschannelbackend.dll','config.ini','qt.conf')) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $file))) { throw "Missing package runtime: $file" }
  }
  $manifest = Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
    [ordered]@{path=$_.FullName.Substring($stage.Length+1).Replace('\','/'); bytes=$_.Length;
      sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
  }
  $manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $workRoot 'payload-manifest.json') -Encoding UTF8
  $isccArguments = @("/DAppVersion=$Version","/DStageDir=$stage", "/DPackageDir=$packageRoot", "/DAppIcon=$(Join-Path $clientRoot 'Whatsapp.ico')")
  if ($SigningConfig) {
    $signCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File $q' +
      (Join-Path $PSScriptRoot 'sign-artifact.ps1') + '$q -Artifact $f -SigningConfig $q' + $SigningConfig + '$q'
    $isccArguments += @('/DSigningEnabled',"/DSigningWorkDir=$workRoot",('/Scppchat=' + $signCommand))
  }
  $isccArguments += (Join-Path $PSScriptRoot 'CppChat.iss')
  Invoke-PackagingTool $IsccPath $isccArguments 'iscc.log'
  $installer = Join-Path $packageRoot "CppChat-Setup-$Version-x64.exe"
  $hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($SigningConfig -and (Get-AuthenticodeSignature -LiteralPath $installer).Status -ne 'Valid') { throw 'Installer signature verification failed.' }
  "$hash  $([IO.Path]::GetFileName($installer))" | Set-Content -LiteralPath "$installer.sha256" -Encoding ASCII
  $updateManifest = [ordered]@{version=$Version; notes=$ReleaseNotes; history=$releaseInfo.history;
    downloadUrl="./$([IO.Path]::GetFileName($installer))"; sha256=$hash;
    size=(Get-Item -LiteralPath $installer).Length}
  $manifestJson = $updateManifest | ConvertTo-Json -Depth 3
  [IO.File]::WriteAllText((Join-Path $packageRoot "CppChat-Update-$Version.json"),$manifestJson,[Text.UTF8Encoding]::new($false))
  Write-Host $(if ($SigningConfig) { 'Code signing: verified' } else { 'Code signing: not configured (unsigned package)' })
  $packageSucceeded = $true
  Write-Host "Installer: $installer"
  Write-Host "Build records: $workRoot"
} finally {
  $env:PATH = $oldPath
  if ($packageSucceeded) {
    $actualStage = (Resolve-Path -LiteralPath $stage).Path
    $allowedRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'build\packaging')) + '\'
    if (-not $actualStage.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
      throw 'Staging cleanup path outside packaging artifacts.'
    }
    Remove-Item -LiteralPath $actualStage -Recurse -Force
  }
}
