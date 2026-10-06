param(
    [string]$ChatServerConfig,
    [string]$Executable
)

$ErrorActionPreference = 'Stop'
$serviceRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $ChatServerConfig) {
    $ChatServerConfig = Join-Path $serviceRoot '..\..\ChatServer\config.ini'
}
if (-not $Executable) {
    $Executable = Join-Path $serviceRoot 'build-logging\Release\CppChatResourceServer.exe'
}
$ChatServerConfig = (Resolve-Path -LiteralPath $ChatServerConfig -ErrorAction Stop).Path
$Executable = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path

if (Get-Process -Name CppChatResourceServer -ErrorAction SilentlyContinue) {
    throw 'ResourceServer is already running; stop it before starting another instance.'
}

$redis = @{}
$section = ''
foreach ($line in Get-Content -LiteralPath $ChatServerConfig) {
    $entry = $line.Trim()
    if ($entry -match '^\[([^]]+)\]$') {
        $section = $Matches[1]
    } elseif ($section -eq 'Redis' -and $entry -match '^([^=]+)=(.*)$') {
        $redis[$Matches[1].Trim()] = $Matches[2].Trim()
    }
}
if (-not $redis['Host'] -or -not $redis['Port'] -or -not $redis['Passwd']) {
    throw 'The ChatServer config is missing Redis Host, Port, or Passwd.'
}

$logDirectory = Join-Path $serviceRoot 'logs'
$dataDirectory = Join-Path $serviceRoot 'resource_data'
New-Item -ItemType Directory -Path $logDirectory, $dataDirectory -Force | Out-Null

$names = @('CPPCHAT_REDIS_HOST', 'CPPCHAT_REDIS_PORT', 'CPPCHAT_REDIS_PASSWORD', 'CPPCHAT_RESOURCE_DIR')
$previous = @{}
foreach ($name in $names) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:CPPCHAT_REDIS_HOST = $redis['Host']
    $env:CPPCHAT_REDIS_PORT = $redis['Port']
    $env:CPPCHAT_REDIS_PASSWORD = $redis['Passwd']
    $env:CPPCHAT_RESOURCE_DIR = $dataDirectory

    $process = Start-Process -FilePath $Executable -WorkingDirectory $serviceRoot `
        -RedirectStandardOutput (Join-Path $logDirectory 'ResourceServer.stdout.log') `
        -RedirectStandardError (Join-Path $logDirectory 'ResourceServer.stderr.log') `
        -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 700
    $process.Refresh()
    if ($process.HasExited) {
        throw "ResourceServer exited during startup (code $($process.ExitCode)); check logs."
    }
    Write-Output "ResourceServer started: PID $($process.Id), port 8093"
    Write-Output "Logs: $logDirectory"
    Write-Output "Image data: $dataDirectory"
} finally {
    foreach ($name in $names) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }
}
