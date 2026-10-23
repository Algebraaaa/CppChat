param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$SqlitePluginRoot,
    [Parameter(Mandatory)][string]$QtRoot,
    [string]$OpenSsl = 'openssl',
    [string]$ReportDirectory = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build')) + [IO.Path]::DirectorySeparatorChar
$reportRoot = if ($ReportDirectory) { [IO.Path]::GetFullPath($ReportDirectory) } else {
    Join-Path $buildRoot ('regression/mobile-session-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
if (-not $reportRoot.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Session probes must write only inside this project build directory'
}
$tlsRoot = Join-Path $reportRoot 'fixture-tls'
New-Item -ItemType Directory -Force $tlsRoot | Out-Null
& $OpenSsl req -x509 -newkey rsa:2048 -sha256 -nodes -days 1 -subj '/CN=localhost' `
    -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' `
    -keyout (Join-Path $tlsRoot 'server.key') -out (Join-Path $tlsRoot 'server.pem') `
    *> (Join-Path $reportRoot 'fixture-generation.log')
if ($LASTEXITCODE -ne 0) { throw 'Cannot generate isolated TLS fixture' }
Copy-Item -LiteralPath (Join-Path $tlsRoot 'server.pem') -Destination (Join-Path $tlsRoot 'ca.pem')
$previous = @{}
foreach ($name in @('Path','QT_QPA_PLATFORM','QT_PLUGIN_PATH','CPPCHAT_TEST_DATA','CPPCHAT_TEST_TLS')) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:Path = "$QtRoot/bin;$env:Path"
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_PLUGIN_PATH = "$SqlitePluginRoot;$QtRoot/plugins"
    $env:CPPCHAT_TEST_DATA = Join-Path $reportRoot 'runtime'
    $env:CPPCHAT_TEST_TLS = $tlsRoot
    $junitOutput = (Join-Path $reportRoot 'session.xml') + ',junitxml'
    & (Join-Path $BuildDirectory 'mobile_session.exe') -o $junitOutput `
        *> (Join-Path $reportRoot 'session.log')
    $probeExit = $LASTEXITCODE
    Get-Content -LiteralPath (Join-Path $reportRoot 'session.log') -Tail 18
    if ($probeExit -ne 0) { throw "Session probe failed; retained evidence: $reportRoot" }
} finally {
    foreach ($name in $previous.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }
    # Only disposable, synthetic TLS keys created by this invocation are removed.
    if ([IO.Path]::GetFullPath($tlsRoot).StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $tlsRoot -Recurse -Force
    }
}
