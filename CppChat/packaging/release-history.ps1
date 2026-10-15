function Get-CppChatReleaseInfo {
  param(
    [Parameter(Mandatory)][string]$HistoryFile,
    [Parameter(Mandatory)][string]$Version,
    [string]$CurrentNotes = ''
  )
  $versionPattern = '^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$'
  if ($Version -notmatch $versionPattern) {
    throw 'Invalid release version.'
  }
  $json = Get-Content -LiteralPath $HistoryFile -Raw -Encoding UTF8
  if (-not $json.TrimStart().StartsWith('[')) { throw 'Release history must be an array.' }
  $history = $json | ConvertFrom-Json
  if ($history.Count -eq 0 -or $history.Count -gt 200) { throw 'Invalid release history size.' }
  $seen = @{}
  foreach ($entry in $history) {
    if ($entry.version -notmatch $versionPattern -or $seen.ContainsKey($entry.version) -or
        -not ($entry.notes -is [string]) -or [string]::IsNullOrWhiteSpace($entry.notes)) {
      throw 'Invalid or duplicate release history entry.'
    }
    $seen[$entry.version] = $true
    if ($entry.version -eq $Version -and $CurrentNotes) { $entry.notes = $CurrentNotes }
  }
  if (-not $seen.ContainsKey($Version)) { throw 'Add this version to release-history.json first.' }
  $entries = @($history | Where-Object { [version]$_.version -le [version]$Version } |
      Sort-Object { [version]$_.version } -Descending)
  $notes = (@($entries | ForEach-Object {
        "v$($_.version)`n$($_.notes.Trim())"
      })) -join "`n`n"
  # Old clients already understand notes, but reject fields over 8000 UTF-16 units.
  if ($notes.Length -gt 8000) { throw 'Condense release history to fit the legacy client notes limit.' }
  return [pscustomobject]@{notes=$notes;history=$entries}
}
