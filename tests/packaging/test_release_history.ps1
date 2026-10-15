param([Parameter(Mandatory)][string]$Artifacts)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $root 'CppChat\packaging\release-history.ps1')
New-Item -ItemType Directory -Path $Artifacts -Force | Out-Null
$source = Join-Path $root 'CppChat\Resources\release-history.json'
function Assert-True([bool]$condition, [string]$message) { if (-not $condition) { throw $message } }
function Assert-Throws([scriptblock]$operation) {
  $caught = $false
  try { & $operation | Out-Null } catch { $caught = $true }
  Assert-True $caught 'Expected validation failure.'
}
$entries = Get-Content -LiteralPath $source -Raw -Encoding UTF8 | ConvertFrom-Json
$latestVersion = ($entries | Sort-Object { [version]$_.version } -Descending | Select-Object -First 1).version
$latest = Get-CppChatReleaseInfo -HistoryFile $source -Version $latestVersion
Assert-True ($latest.history.Count -eq $entries.Count) 'Missing historical releases.'
Assert-True ($latest.notes.Contains('v0.1.0') -and $latest.notes.Contains("v$latestVersion")) 'Legacy notes lack history.'
Assert-True ($latest.notes.Length -le 8000) 'Legacy notes exceed size limit.'
$old = Get-CppChatReleaseInfo -HistoryFile $source -Version '0.1.2' -CurrentNotes '测试覆盖本版本说明'
Assert-True ($old.history.Count -eq 3 -and -not $old.notes.Contains('v0.1.7')) 'Future notes leaked into an old release.'
Assert-True ($old.notes.Contains('测试覆盖本版本说明') -and $old.notes.Contains('v0.1.0')) 'Override dropped history.'
Assert-Throws { Get-CppChatReleaseInfo -HistoryFile $source -Version '9.9.9' }
Assert-Throws { Get-CppChatReleaseInfo -HistoryFile $source -Version 'invalid' }
$fixture = Join-Path $Artifacts 'release-history-fixture.json'
@([pscustomobject]@{version='0.1.9';notes='Nine'}, [pscustomobject]@{version='0.1.10';notes='Ten'}) | ConvertTo-Json | Set-Content -LiteralPath $fixture -Encoding UTF8
$ordered = Get-CppChatReleaseInfo -HistoryFile $fixture -Version '0.1.10'
Assert-True ($ordered.history[0].version -eq '0.1.10') 'History sorted lexicographically.'
($entries + $entries[0]) | ConvertTo-Json | Set-Content -LiteralPath $fixture -Encoding UTF8
Assert-Throws { Get-CppChatReleaseInfo -HistoryFile $fixture -Version '0.1.7' }
$latest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Artifacts 'release-info.json') -Encoding UTF8
Write-Host 'PASS: complete history, legacy notes, override, version ordering, missing/invalid versions and duplicate validation.'
