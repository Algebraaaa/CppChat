param([Parameter(Mandatory=$true)][string]$Artifacts)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$artifactRoot=[IO.Path]::GetFullPath($Artifacts)
if (-not $artifactRoot.StartsWith((Join-Path $repository 'build')+[IO.Path]::DirectorySeparatorChar,
                                 [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Viewer test artifacts must be inside the repository build directory'
}
$directory=Join-Path $artifactRoot ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
try {
  $tokens=$null
  $parseErrors=$null
  $script=Join-Path $repository '.runtime/cppchat.ps1'
  $ast=[Management.Automation.Language.Parser]::ParseFile($script,[ref]$tokens,[ref]$parseErrors)
  if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
  $functions=$ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$true)
  foreach ($name in @('Initialize-LogFileIdentity','Read-ServerLogTail')) {
    $function=$functions | Where-Object Name -eq $name
    if (-not $function) { throw "Missing production helper: $name" }
    Invoke-Expression $function.Extent.Text
  }
  Initialize-LogFileIdentity
  $path=Join-Path $directory 'events.log'
  $encoding=[Text.UTF8Encoding]::new($false)
  [IO.File]::WriteAllText($path,"first 中文`n",$encoding)
  $state=@{}
  if ((Read-ServerLogTail $state $path) -notmatch 'first 中文') { throw 'Initial tail lost text' }
  if (Read-ServerLogTail $state $path) { throw 'Unchanged content repeated' }
  [IO.File]::AppendAllText($path,"appended`n",$encoding)
  $text=Read-ServerLogTail $state $path
  if ($text -notmatch 'appended' -or $text -match 'first') { throw 'Append position incorrect' }
  $creation=[IO.File]::GetCreationTimeUtc($path)
  Move-Item -LiteralPath $path -Destination (Join-Path $directory 'events.1.log')
  [IO.File]::WriteAllText($path,("rotated`n"*30),$encoding)
  [IO.File]::SetCreationTimeUtc($path,$creation)
  if ((Read-ServerLogTail $state $path) -ne ("rotated`n"*30)) {
    throw 'Rotation to a larger file with matching timestamps was not detected'
  }
  [IO.File]::WriteAllText($path,"truncated`n",$encoding)
  if ((Read-ServerLogTail $state $path) -ne "truncated`n") { throw 'Truncation not detected' }
  Write-Output 'PASS: parse, UTF-8, append, no duplicate, rotation identity, truncation, released handle'
} finally {
  $resolved=[IO.Path]::GetFullPath($directory)
  if ($resolved.StartsWith($artifactRoot+[IO.Path]::DirectorySeparatorChar,
                           [StringComparison]::OrdinalIgnoreCase)) {
    Remove-Item -LiteralPath $resolved -Recurse -Force
  }
}
