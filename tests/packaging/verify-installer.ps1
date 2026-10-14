param(
  [Parameter(Mandatory=$true)][string]$Installer,
  [Parameter(Mandatory=$true)][string]$Manifest,
  [string]$Probe,
  [string]$SecurityProbe
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Probe) { $Probe = Join-Path $repositoryRoot 'CppChat\build\release\tests\qt\package_probe.exe' }
if (-not $SecurityProbe) { $SecurityProbe = Join-Path $repositoryRoot 'CppChat\build\release\tests\qt\security_store_probe.exe' }
$Installer = (Resolve-Path -LiteralPath $Installer).Path
$Probe = (Resolve-Path -LiteralPath $Probe).Path
$SecurityProbe = (Resolve-Path -LiteralPath $SecurityProbe).Path
$payload = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$appKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{D84D25B0-04A5-48F8-B3C9-04937B05E2AD}_is1'
foreach ($registeredKey in @($appKey,
    'HKCU:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{D84D25B0-04A5-48F8-B3C9-04937B05E2AD}_is1',
    'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{D84D25B0-04A5-48F8-B3C9-04937B05E2AD}_is1',
    'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{D84D25B0-04A5-48F8-B3C9-04937B05E2AD}_is1')) {
  if (Test-Path -LiteralPath $registeredKey) { throw 'CppChat is already registered; run this test in a clean user environment.' }
}
$programs = [Environment]::GetFolderPath('Programs')
$originalShortcuts = @{}
Get-ChildItem -LiteralPath (Join-Path $programs 'CppChat') -File -ErrorAction SilentlyContinue | ForEach-Object {
  $originalShortcuts[$_.FullName] = (Get-FileHash -LiteralPath $_.FullName).Hash
}
$runId = [Guid]::NewGuid().ToString('N').Substring(0,8)
$workRoot = Join-Path $repositoryRoot "build\packaging\verification-$runId"
$installation = Join-Path $workRoot 'Install With Spaces'
$group = "CppChat Packaging Test $runId"
New-Item -ItemType Directory -Path $workRoot | Out-Null
function Run-Setup([string]$Executable, [string]$Arguments) {
  $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru
  $null = $process.Handle
  if (-not $process.WaitForExit(60000)) {
    Stop-Process -Id $process.Id -Force
    throw 'Installer process timed out.'
  }
  if ($process.ExitCode -ne 0) { throw "Installer exit code $($process.ExitCode)" }
}
function Verify-Payload {
  foreach ($file in $payload) {
    if ($file.path -notlike 'licenses/Encrypted-SQLite-Source/*' -and
        $file.path -match '(^|/)(chat_data|logs|accounts|tests|\.runtime)(/|$)|\.(sqlite|db|pdb|cpp|h|pem|key|pfx|p12)$|secrets') {
      throw "Private or development file in installer: $($file.path)"
    }
    $path = Join-Path $installation $file.path
    if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $file.sha256) {
      throw "Installed payload differs: $($file.path)"
    }
  }
}
function Run-DataProbe([string]$Mode, [string]$Directory, [string]$Name) {
  $output = Join-Path $workRoot "$Name.json"
  $process = Start-Process -FilePath (Join-Path $installation 'security_store_probe.exe') -ArgumentList "$Mode `"$Directory`"" -WorkingDirectory $installation -WindowStyle Hidden -PassThru -RedirectStandardOutput $output -RedirectStandardError (Join-Path $workRoot "$Name-stderr.log")
  $null = $process.Handle
  if (-not $process.WaitForExit(15000)) { Stop-Process -Id $process.Id -Force; throw 'Data probe timed out.' }
  if ($process.ExitCode -ne 0) { throw "Data probe failed: $Name" }
  return (Get-Content -LiteralPath $output -Raw).Trim()
}
$setupArguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /DIR=`"$installation`" /GROUP=`"$group`" /TASKS=`"`""
$oldPath = $env:PATH
$oldPluginPath = $env:QT_PLUGIN_PATH
$oldQpaPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
$oldPlatform = $env:QT_QPA_PLATFORM
$passed = $false
try {
  Run-Setup $Installer "$setupArguments /LOG=`"$workRoot\install.log`""
  Verify-Payload
  # DefaultGroupName explicitly honors /GROUP even when the group page is hidden.
  $actualGroup = (Get-ItemProperty -LiteralPath $appKey).'Inno Setup: Icon Group'
  if ($actualGroup -ne $group) { throw 'Installer did not use the isolated test shortcut group.' }
  foreach ($original in $originalShortcuts.Keys) {
    if ((Get-FileHash -LiteralPath $original).Hash -ne $originalShortcuts[$original]) { throw 'An existing shortcut changed.' }
  }
  $shortcut = Join-Path $programs "$actualGroup\CppChat.lnk"
  if (-not (Test-Path -LiteralPath $shortcut)) { throw 'Start menu shortcut is missing.' }
  $shell = New-Object -ComObject WScript.Shell
  if ($shell.CreateShortcut($shortcut).TargetPath -ne (Join-Path $installation 'CppChat.exe')) {
    throw 'Start menu shortcut target differs.'
  }
  Copy-Item -LiteralPath $Probe -Destination (Join-Path $installation 'package_probe.exe')
  Copy-Item -LiteralPath $SecurityProbe -Destination (Join-Path $installation 'security_store_probe.exe')
  $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
  $env:QT_PLUGIN_PATH = $null
  $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
  $env:QT_QPA_PLATFORM = $null
  $probeProcess = Start-Process -FilePath (Join-Path $installation 'package_probe.exe') -WorkingDirectory $installation -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $workRoot 'runtime.json') -RedirectStandardError (Join-Path $workRoot 'runtime-stderr.log')
  $null = $probeProcess.Handle
  if (-not $probeProcess.WaitForExit(15000)) {
    Stop-Process -Id $probeProcess.Id -Force
    throw 'Packaged runtime probe timed out.'
  }
  if ($probeProcess.ExitCode -ne 0) { throw "Packaged runtime failed (exit $($probeProcess.ExitCode)): $workRoot\runtime-stderr.log" }
  $env:PATH = $oldPath
  $markerDirectory = Join-Path $installation 'chat_data\accounts\packaging-test\101\sql'
  New-Item -ItemType Directory -Path $markerDirectory -Force | Out-Null
  $marker = Join-Path $markerDirectory 'preserve-data.txt'
  'packaging-test-owned-data' | Set-Content -LiteralPath $marker -Encoding ASCII
  $markerHash = (Get-FileHash -LiteralPath $marker).Hash
  $privateAccount = Join-Path $installation 'chat_data\accounts\0123456789abcdef01234567\101'
  $dataBefore = Run-DataProbe 'create' $privateAccount 'data-before'
  Run-Setup $Installer "$setupArguments /LOG=`"$workRoot\upgrade.log`""
  Verify-Payload
  $dataAfter = Run-DataProbe 'audit' $privateAccount 'data-after'
  if ($dataBefore -ne $dataAfter) { throw 'Encryption migration changed chat messages or drafts.' }
  $header = [IO.File]::ReadAllBytes((Join-Path $privateAccount 'sql\db.sqlite'))
  if ([Text.Encoding]::ASCII.GetString($header,0,15) -eq 'SQLite format 3') { throw 'Database is still plaintext.' }
  $image = [IO.File]::ReadAllBytes((Join-Path $privateAccount 'attachments\package-image'))
  if ([Text.Encoding]::ASCII.GetString($image) -match 'package-image-secret') { throw 'Attachment is still plaintext.' }
  if ((Get-FileHash -LiteralPath $marker).Hash -ne $markerHash) { throw 'Update changed user data.' }
  $uninstaller = Join-Path $installation 'unins000.exe'
  Run-Setup $uninstaller "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=`"$workRoot\uninstall.log`""
  if (Test-Path -LiteralPath $appKey) { throw 'Uninstall registration remains.' }
  if (Test-Path -LiteralPath (Join-Path $installation 'CppChat.exe')) { throw 'Application EXE remains after uninstall.' }
  if ((Get-FileHash -LiteralPath $marker).Hash -ne $markerHash) { throw 'Uninstall changed user data.' }
  if (-not (Test-Path -LiteralPath (Join-Path $privateAccount '.key.dpapi'))) { throw 'Uninstall removed the data decryption key.' }
  if (Test-Path -LiteralPath $shortcut) { throw 'Uninstall left the start menu shortcut.' }
  foreach ($original in $originalShortcuts.Keys) {
    if ((Get-FileHash -LiteralPath $original).Hash -ne $originalShortcuts[$original]) { throw 'Uninstall changed an existing shortcut.' }
  }
  $passed = $true
  'PASS: installation, file hashes, start menu shortcut, isolated Qt/SQLite/TLS runtime, update and uninstall data preservation.' | Set-Content -LiteralPath (Join-Path $workRoot 'result.txt')
  Get-Content -LiteralPath (Join-Path $workRoot 'runtime.json')
  Get-Content -LiteralPath (Join-Path $workRoot 'result.txt')
} finally {
  $env:PATH = $oldPath
  $env:QT_PLUGIN_PATH = $oldPluginPath
  $env:QT_QPA_PLATFORM_PLUGIN_PATH = $oldQpaPath
  $env:QT_QPA_PLATFORM = $oldPlatform
  if ($passed) {
    $actual = (Resolve-Path -LiteralPath $installation).Path
    $allowed = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'build\packaging')) + '\'
    if (-not $actual.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Cleanup path outside test artifacts.' }
    Remove-Item -LiteralPath $actual -Recurse -Force
  }
}
