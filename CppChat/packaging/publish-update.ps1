param(
  [Parameter(Mandatory=$true)]
  [ValidatePattern('^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$')][string]$Version,
  [ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_.-]*$')][string]$SshHost = 'huawei'
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$packages = Join-Path $repositoryRoot 'build\packages'
$installer = Join-Path $packages "CppChat-Setup-$Version-x64.exe"
$manifest = Join-Path $packages "CppChat-Update-$Version.json"
$release = Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json
if ($release.version -ne $Version -or $release.downloadUrl -ne "./CppChat-Setup-$Version-x64.exe" -or
    $release.size -ne (Get-Item -LiteralPath $installer).Length -or
    $release.sha256 -ne (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()) {
  throw 'Update manifest does not match the completed installer.'
}
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$workRoot = Join-Path $repositoryRoot "build\packaging\publish-$runId"
New-Item -ItemType Directory -Path $workRoot | Out-Null
$incoming = "/opt/cppchat/updates/.incoming-$runId"
function Run-Native([string]$Tool, [string[]]$Arguments, [string]$LogName) {
  $oldPreference = $ErrorActionPreference
  try {
    $ErrorActionPreference = 'Continue'
    & $Tool @Arguments *> (Join-Path $workRoot $LogName)
    $result = $LASTEXITCODE
  } finally { $ErrorActionPreference = $oldPreference }
  if ($result -ne 0) { throw "Publish step failed ($result): $workRoot\$LogName. Remote staging retained." }
}
Run-Native 'ssh.exe' @('-o','BatchMode=yes','-o','ConnectTimeout=15',$SshHost,
  "install -d -m 755 '$incoming'") 'prepare.log'
Run-Native 'scp.exe' @('-o','BatchMode=yes','-o','ConnectTimeout=15',$installer,$manifest,
  (Join-Path $PSScriptRoot 'deploy-updates.py'),"${SshHost}:$incoming/") 'upload.log'
Run-Native 'ssh.exe' @('-o','BatchMode=yes','-o','ConnectTimeout=15',$SshHost,
  "python3 '$incoming/deploy-updates.py' --incoming '$incoming' --version '$Version'") 'publish.log'
Get-Content -LiteralPath (Join-Path $workRoot 'publish.log')
Write-Host "Publish records: $workRoot"
