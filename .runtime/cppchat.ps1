param(
  [Parameter(Position=0)][ValidateSet('start','stop','build','debug','debug-client','sync','help','log-window')][string]$Action='start',
  [ValidateSet('Release','Debug')][string]$Configuration='Release',
  [ValidateSet('All','Server','Client','GateServer','StatusServer','ChatServer','ChatServer2','ResourceServer')][string]$Target='All',
  [ValidateSet('GateServer','StatusServer','ChatServer','ChatServer2','ResourceServer')][string]$Server='GateServer',
  [ValidateRange(1,64)][int]$Jobs=8,
  [switch]$InstallDependencies,
  [switch]$NoClient,
  [switch]$InfrastructureOnly,
  [ValidateSet('GateServer','StatusServer','ChatServer','ChatServer2','ResourceServer','VerifyServer')][string]$LogServer,
  [int]$LogProcessId
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$node='D:\msys64\ucrt64\bin\node.exe'

function Invoke-Redis([ValidateSet('ping','shutdown')][string]$Command) {
  # Use the existing Node dependency without a separate Redis helper file.
  $redisCode=@'
const path = require('node:path');
const root = process.argv[1];
const command = process.argv[2];
const Redis = require(path.join(root, 'VerifyServer', 'node_modules', 'ioredis'));
const config = require(path.join(root, 'VerifyServer', 'config.json')).redis;
const client = new Redis({...config, connectTimeout: 2000, maxRetriesPerRequest: 0, retryStrategy: () => null});
client.on('error', () => {});
(async () => {
  try {
    if (command === 'shutdown') {
      await client.del('chat_node_ChatServer1', 'chat_node_ChatServer2');
      await client.shutdown('save');
    } else if (await client.ping() !== 'PONG') throw new Error('Redis ping failed');
    client.disconnect();
  } catch (error) {
    client.disconnect();
    if (command !== 'shutdown' || !error.message.includes('closed')) {
      if (command === 'shutdown') console.error('Redis shutdown failed:', error.message);
      process.exitCode = 1;
    }
  }
})();
'@
  $ErrorActionPreference='Continue'
  & $node -e $redisCode $PSScriptRoot $Command
  return $LASTEXITCODE
}

function Sync-Configuration {
  $repositoryRoot=Split-Path $PSScriptRoot -Parent
  $copied=0
  foreach($component in @('GateServer','StatusServer','ChatServer','ChatServer2','VerifyServer')) {
    $file=if($component -eq 'VerifyServer'){'config.json'}else{'config.ini'}
    $source=Join-Path $repositoryRoot "$component\$file"
    $destination=Join-Path $PSScriptRoot "$component\$file"
    if(-not(Test-Path -LiteralPath $source)){throw "Missing local configuration: $source"}
    if($file -eq 'config.json') {Get-Content -LiteralPath $source -Raw | ConvertFrom-Json | Out-Null}
    New-Item -ItemType Directory -Force (Split-Path $destination -Parent) | Out-Null
    if(-not(Test-Path -LiteralPath $destination) -or (Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
      Copy-Item -LiteralPath $source -Destination $destination -Force
      $copied++
    }
  }
  $clientSource=Join-Path $repositoryRoot 'CppChat\config.ini'
  if(-not(Test-Path -LiteralPath $clientSource)){throw "Missing local client configuration: $clientSource"}
  foreach($configuration in @('debug','release')) {
    $directory=Join-Path $repositoryRoot "CppChat\build\$configuration"
    if(-not(Test-Path -LiteralPath $directory)){continue}
    $destination=Join-Path $directory 'config.ini'
    if(-not(Test-Path -LiteralPath $destination) -or (Get-FileHash -LiteralPath $clientSource).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
      Copy-Item -LiteralPath $clientSource -Destination $destination -Force
      $copied++
    }
  }
  Write-Host "Local component configurations synchronized ($copied files updated)."
}

function Get-ServerLogWindow([string]$Name,[int]$ServiceProcessId) {
  $stateFile=Join-Path $PSScriptRoot "$Name\log-window.pid"
  if (-not (Test-Path -LiteralPath $stateFile)) { return }
  $windowProcessId=0
  if (-not [int]::TryParse((Get-Content -LiteralPath $stateFile -Raw).Trim(),[ref]$windowProcessId)) { return }
  $candidate=Get-CimInstance Win32_Process -Filter "ProcessId=$windowProcessId" -ErrorAction SilentlyContinue
  if (-not $candidate -or $candidate.Name -ne 'powershell.exe') { return }
  $commandLine=[string]$candidate.CommandLine
  if (-not $commandLine.Contains("$PSScriptRoot\cppchat.ps1") -or
      $commandLine -notmatch '-Action\s+log-window(?:\s|$)' -or
      $commandLine -notmatch "-LogServer\s+$Name(?:\s|$)" -or
      $commandLine -notmatch "-LogProcessId\s+$ServiceProcessId(?:\s|$)") { return }
  Get-Process -Id $windowProcessId -ErrorAction SilentlyContinue
}

function Show-ServerLogWindow([string]$Name,[int]$ServiceProcessId) {
  if (Get-ServerLogWindow $Name $ServiceProcessId) { return }
  $directory=Join-Path $PSScriptRoot $Name
  $shellExecutable="$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
  $shellArguments="-NoLogo -NoProfile -ExecutionPolicy Bypass -File `"$PSScriptRoot\cppchat.ps1`" -Action log-window -LogServer $Name -LogProcessId $ServiceProcessId"
  $terminal=Get-Command wt.exe -ErrorAction SilentlyContinue
  if ($terminal) {
    # Explicitly request a separate Terminal window for each server.
    $terminalArguments="-w new new-tab --title `"CppChat - $Name`" --suppressApplicationTitle -d `"$directory`" `"$shellExecutable`" $shellArguments"
    Start-Process -FilePath $terminal.Source -ArgumentList $terminalArguments -WorkingDirectory $directory -WindowStyle Normal
  } else {
    Start-Process -FilePath $shellExecutable -ArgumentList $shellArguments -WorkingDirectory $directory -WindowStyle Normal
  }
  for ($attempt=0; $attempt -lt 40; $attempt++) {
    if (Get-ServerLogWindow $Name $ServiceProcessId) { return }
    Start-Sleep -Milliseconds 250
  }
  throw "Could not open the $Name log window; service PID $ServiceProcessId is still running."
}

function Watch-ServerLogs {
  if (-not $LogServer -or $LogProcessId -le 0) { throw 'Log window requires a server name and process ID.' }
  $serviceProcess=Get-Process -Id $LogProcessId -ErrorAction Stop
  $directory=Join-Path $PSScriptRoot $LogServer
  $Host.UI.RawUI.WindowTitle="CppChat - $LogServer"
  [Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
  $PID | Set-Content -LiteralPath (Join-Path $directory 'log-window.pid')
  Write-Host "=== $LogServer | PID $LogProcessId ===" -ForegroundColor Cyan
  Write-Host 'Live stdout and stderr. Stop services using CppChat.cmd stop.'
  Write-Host "Log directory: $directory"
  $readers=@()
  try {
    foreach ($file in @('stdout.log','stderr.log')) {
      $path=Join-Path $directory $file
      if (-not (Test-Path -LiteralPath $path)) { continue }
      $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
      $reader=[IO.StreamReader]::new($stream,[Text.UTF8Encoding]::new($false),$true)
      $initial=$reader.ReadToEnd()
      foreach ($line in @($initial -split '\r?\n' | Select-Object -Last 60)) {
        if ($line) {
          if ($file -eq 'stderr.log') { Write-Host $line -ForegroundColor Red } else { Write-Host $line }
        }
      }
      $readers+=@{Reader=$reader;IsError=($file -eq 'stderr.log')}
    }
    do {
      foreach ($entry in $readers) {
        $reader=$entry.Reader
        if ($reader.BaseStream.Position -gt $reader.BaseStream.Length) {
          $reader.DiscardBufferedData()
          [void]$reader.BaseStream.Seek(0,[IO.SeekOrigin]::Begin)
        }
        $appended=$reader.ReadToEnd()
        if ($appended) {
          if ($entry.IsError) { Write-Host $appended -NoNewline -ForegroundColor Red } else { Write-Host $appended -NoNewline }
        }
      }
      Start-Sleep -Milliseconds 250
      $serviceProcess.Refresh()
    } while (-not $serviceProcess.HasExited)
  } finally {
    foreach ($entry in $readers) { $entry.Reader.Dispose() }
  }
}

function Start-Project {
  param(
    [switch]$NoClient,
    [ValidateSet('Release','Debug')][string]$Configuration='Release',
    [ValidateSet('GateServer','StatusServer','ChatServer','ChatServer2','ResourceServer')][string[]]$SkipServer=@(),
    [switch]$InfrastructureOnly
  )
  [Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
  Sync-Configuration
  function Wait-Port([int]$Port) {
    for ($attempt=0; $attempt -lt 60; $attempt++) {
      $socket = [Net.Sockets.TcpClient]::new()
      try {
        $connection = $socket.ConnectAsync('127.0.0.1', $Port)
        if ($connection.Wait(500) -and $socket.Connected) { return }
      } catch {} finally { $socket.Dispose() }
      Start-Sleep -Milliseconds 500
    }
    throw "Port $Port did not become ready; inspect .runtime logs"
  }
  function Start-Component([string]$Name,[string]$Executable,[string[]]$Arguments,[int]$Port) {
    $directory = Join-Path $PSScriptRoot $Name
    New-Item -ItemType Directory -Force $directory | Out-Null
    $pidFile = Join-Path $directory 'windows.pid'
    $allowedPaths = if ($Name -eq 'VerifyServer') { @($Executable) } else {
      @("$root\build\server\bin\$Name\Debug\$Name.exe", "$root\build\server\bin\$Name\Release\$Name.exe")
    }
    # The listener is authoritative; PID files may be missing or left from an earlier launch.
    $listenerIds = @(Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
      Select-Object -ExpandProperty OwningProcess -Unique)
    if ($listenerIds.Count -gt 1) { throw "Port $Port has multiple listeners (PIDs $($listenerIds -join ', ')); cannot reuse $Name." }
    $ownedProcess = $null
    if ($listenerIds.Count -eq 1) {
      $ownedProcess = Get-Process -Id $listenerIds[0] -ErrorAction SilentlyContinue
    } elseif (Test-Path -LiteralPath $pidFile) {
      $savedProcessId = 0
      if ([int]::TryParse((Get-Content -LiteralPath $pidFile -Raw).Trim(), [ref]$savedProcessId) -and $savedProcessId -gt 0) {
        $ownedProcess = Get-Process -Id $savedProcessId -ErrorAction SilentlyContinue
      }
    }
    $isOwned = $ownedProcess -and $ownedProcess.Path -in $allowedPaths
    if ($isOwned -and $Name -eq 'VerifyServer') {
      $commandLine = ([string](Get-CimInstance Win32_Process -Filter "ProcessId=$($ownedProcess.Id)").CommandLine).Replace('/','\')
      $isOwned = $commandLine.Contains("$PSScriptRoot\VerifyServer\server.js")
    }
    if ($isOwned) {
      Wait-Port $Port
      $ownedProcess.Id | Set-Content -LiteralPath $pidFile
      Show-ServerLogWindow $Name $ownedProcess.Id
      $runningConfiguration = if ($Name -eq 'VerifyServer') { 'Node.js' } else { Split-Path (Split-Path $ownedProcess.Path -Parent) -Leaf }
      Write-Host "$Name already running ($runningConfiguration, PID $($ownedProcess.Id), port $Port)"
      return
    }
    if ($listenerIds.Count) {
      $processDescription = if ($ownedProcess) { "$($ownedProcess.ProcessName), path $($ownedProcess.Path)" } else { 'process details unavailable' }
      throw "Port $Port is occupied by PID $($listenerIds[0]) ($processDescription), which could not be verified as this project's $Name."
    }
    if (-not (Test-Path $Executable)) { throw "Missing executable: $Executable; run CppChat.cmd build first" }
    $parameters = @{FilePath=$Executable;WorkingDirectory=$directory;WindowStyle='Hidden';PassThru=$true;RedirectStandardOutput="$directory\stdout.log";RedirectStandardError="$directory\stderr.log"}
    if ($Arguments) { $parameters.ArgumentList=$Arguments }
    $started = Start-Process @parameters
    $started.Id | Set-Content $pidFile
    Wait-Port $Port
    $started.Refresh()
    if ($started.HasExited) { throw "$Name exited during startup; inspect $directory\stderr.log" }
    Show-ServerLogWindow $Name $started.Id
    Write-Host "$Name ready on $Port"
  }
  $mysqlService = Get-Service -Name MySQL84
  if ($mysqlService.Status -ne 'Running') {
    try { Start-Service -Name MySQL84 -ErrorAction Stop }
    catch { throw 'MySQL84 is stopped. Start the MySQL84 Windows service as administrator, then retry.' }
  }
  Wait-Port 3306
  $mysqlPort = & D:\SoftWare\MySQL\Server8.4\bin\mysql.exe "--defaults-extra-file=$PSScriptRoot\mysql\client.ini" --protocol=TCP --connect-timeout=5 --batch --skip-column-names -e 'SELECT @@port;'
  if ($LASTEXITCODE -ne 0 -or "$mysqlPort".Trim() -ne '3306') { throw 'MySQL84 authentication or port check failed.' }
  Write-Host 'MySQL84 ready on 3306'
  $redisPingExit=Invoke-Redis 'ping'
  if ($redisPingExit -ne 0) {
    & wsl.exe -d Ubuntu -u root -- redis-server /mnt/d/Project/CppChat/.runtime/redis/redis.conf
    if ($LASTEXITCODE -ne 0) { throw 'Redis startup failed' }
    Wait-Port 6380
    if ((Invoke-Redis 'ping') -ne 0) { throw 'Redis authentication failed' }
  }
  Write-Host 'Redis ready on 6380'
  Start-Component 'VerifyServer' $node @("$PSScriptRoot\VerifyServer\server.js") 50051
  if ($InfrastructureOnly) { Write-Host 'Debug infrastructure is ready: MySQL, Redis, VerifyServer'; return }
  if ('StatusServer' -notin $SkipServer) { Start-Component 'StatusServer' "$root\build\server\bin\StatusServer\$Configuration\StatusServer.exe" @() 50052 }
  if ('ChatServer' -notin $SkipServer) { Start-Component 'ChatServer' "$root\build\server\bin\ChatServer\$Configuration\ChatServer.exe" @() 8091 }
  if ('ChatServer2' -notin $SkipServer) { Start-Component 'ChatServer2' "$root\build\server\bin\ChatServer2\$Configuration\ChatServer2.exe" @() 8092 }
  $resourceConfig = Get-Content "$PSScriptRoot\secrets.json" -Raw | ConvertFrom-Json
  $env:CPPCHAT_REDIS_HOST='127.0.0.1'
  $env:CPPCHAT_REDIS_PORT='6380'
  $env:CPPCHAT_REDIS_PASSWORD=$resourceConfig.redis
  $env:CPPCHAT_RESOURCE_DIR="$PSScriptRoot\ResourceServer\resource_data"
  if ('ResourceServer' -notin $SkipServer) { Start-Component 'ResourceServer' "$root\build\server\bin\ResourceServer\$Configuration\ResourceServer.exe" @() 8093 }
  Remove-Item Env:\CPPCHAT_REDIS_PASSWORD
  if ('GateServer' -notin $SkipServer) { Start-Component 'GateServer' "$root\build\server\bin\GateServer\$Configuration\GateServer.exe" @() 8090 }
  if (-not $NoClient) {
    New-Item -ItemType Directory -Force "$root\CppChat\.runtime" | Out-Null
    $executable="$root\CppChat\build\release\CppChat.exe"
    if ($Configuration -eq 'Debug') { $executable="$root\CppChat\build\debug\CppChat.exe" }
    $ownedClient=$null
    if (Test-Path "$root\CppChat\.runtime\client.pid") {
      $ownedClient=Get-Process -Id ([int](Get-Content "$root\CppChat\.runtime\client.pid")) -ErrorAction SilentlyContinue
    }
    if (-not $ownedClient -or $ownedClient.Path -ne $executable) {
      $started=Start-Process $executable -WorkingDirectory (Split-Path $executable -Parent) -PassThru
      $started.Id | Set-Content "$root\CppChat\.runtime\client.pid"
    }
  }
  Write-Host 'CppChat is ready. Local verification emails: .runtime\mail'
}

function Stop-Project {
  if (Test-Path "$root\CppChat\.runtime\client.pid") {
    $client=Get-Process -Id ([int](Get-Content "$root\CppChat\.runtime\client.pid")) -ErrorAction SilentlyContinue
    if ($client -and $client.Path -in @("$root\CppChat\build\release\CppChat.exe", "$root\CppChat\build\debug\CppChat.exe")) {
      [void]$client.CloseMainWindow()
      if (-not $client.WaitForExit(5000)) { $client | Stop-Process }
    }
  }
  foreach ($name in @('GateServer','ResourceServer','ChatServer2','ChatServer','StatusServer','VerifyServer')) {
    $pidFile="$PSScriptRoot\$name\windows.pid"
    if (-not (Test-Path $pidFile)) { continue }
    $process=Get-Process -Id ([int](Get-Content $pidFile)) -ErrorAction SilentlyContinue
    $executables=if ($name -eq 'VerifyServer') {@('D:\msys64\ucrt64\bin\node.exe')} else {@("$root\build\server\bin\$name\Release\$name.exe", "$root\build\server\bin\$name\Debug\$name.exe")}
    if ($process -and $name -eq 'VerifyServer') {
      $commandLine=([string](Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)").CommandLine).Replace('/','\')
      if (-not $commandLine.Contains("$PSScriptRoot\VerifyServer\server.js")) { $process=$null }
    }
    if ($process -and $process.Path -in $executables) {
      $logWindow=Get-ServerLogWindow $name $process.Id
      $process | Stop-Process
      if ($logWindow) { [void]$logWindow.WaitForExit(5000) }
    }
  }
  # Redis flushes its data before exiting. The shared MySQL84 Windows service stays running.
  [void](Invoke-Redis 'shutdown')
  # MySQL84 also serves normal local mysql CLI sessions; project shutdown does not stop it.
  Write-Host 'Project processes stopped. Shared MySQL84 remains running on 3306.'
}

function Build-Project {
  Sync-Configuration
  $serverCmake='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
  $env:TEMP='D:\Dev\tmp'
  $env:TMP=$env:TEMP
  $env:VCPKG_DOWNLOADS='D:\Dev\cache\vcpkg\downloads'
  $env:VCPKG_BINARY_SOURCES='clear;files,D:\Dev\cache\vcpkg\archives,readwrite'
  $env:VCPKG_FORCE_SYSTEM_BINARIES='1'
  $env:VCPKG_MAX_CONCURRENCY="$Jobs"
  $env:PATH="$(Split-Path $serverCmake);D:\Dev\tools\powershell;D:\SoftWare\Git\Git\cmd;D:\Qt\Tools\mingw1310_64\bin;D:\Qt\Tools\Ninja;D:\Qt\6.12.0\mingw_64\bin;D:\Dev\cache\vcpkg\downloads\tools\7zip-26.03-windows;"+$env:PATH
  if ($InstallDependencies) {
    $env:HTTPS_PROXY='http://127.0.0.1:7897'
    $env:HTTP_PROXY=$env:HTTPS_PROXY
    & D:\Dev\vcpkg\vcpkg.exe install boost-asio boost-beast boost-filesystem boost-uuid boost-property-tree grpc jsoncpp hiredis openssl 'mysql-connector-cpp[jdbc]' --triplet=x64-windows-static-md-release --host-triplet=x64-windows-static-md-release --overlay-triplets=D:\Dev\vcpkg-triplets --clean-buildtrees-after-build --clean-packages-after-build
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg dependency installation failed' }
  }
  if ($Target -ne 'Client') {
    & $serverCmake -S $root -B "$root\build\server" -G 'Visual Studio 18 2026' -A x64 -T v145 -DCMAKE_TOOLCHAIN_FILE=D:/Dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md-release -DVCPKG_HOST_TRIPLET=x64-windows-static-md-release -DVCPKG_OVERLAY_TRIPLETS=D:/Dev/vcpkg-triplets
    if ($LASTEXITCODE -ne 0) { throw 'Server configuration failed' }
    Copy-Item -LiteralPath "$PSScriptRoot\ide\CppChatServers.slnLaunch" -Destination "$root\build\server\CppChatServers.slnLaunch"
    $buildArguments=@('--build',"$root\build\server",'--config',$Configuration,'--parallel',"$Jobs")
    if ($Target -notin @('All','Server')) { $buildArguments+=@('--target',$Target) }
    & $serverCmake @buildArguments
    if ($LASTEXITCODE -ne 0) { throw 'Server build failed' }
  }
  if ($Target -in @('All','Client')) {
    Push-Location "$root\CppChat"
    try {
      $preset='local-'+$Configuration.ToLowerInvariant()
      & D:\Qt\Tools\CMake_64\bin\cmake.exe --preset $preset
      if ($LASTEXITCODE -ne 0) { throw 'Client configuration failed' }
      & D:\Qt\Tools\CMake_64\bin\cmake.exe --build --preset $preset --parallel $Jobs
      if ($LASTEXITCODE -ne 0) { throw 'Client build failed' }
    } finally { Pop-Location }
  }
}

function Open-DebugEnvironment([switch]$ClientOnly) {
  $chatNodesStopped=$false
  foreach ($name in @('GateServer','StatusServer','ChatServer','ChatServer2','ResourceServer')) {
    $ownedPaths=@("$root\build\server\bin\$name\Release\$name.exe")
    if (-not $ClientOnly -and $name -eq $Server) {
      $ownedPaths += "$root\build\server\bin\$name\Debug\$name.exe"
    }
    foreach ($running in @(Get-Process -Name $name -ErrorAction SilentlyContinue | Where-Object {$_.Path -in $ownedPaths})) {
      $running | Stop-Process
      $running.WaitForExit()
      if ($name -in @('ChatServer','ChatServer2')) { $chatNodesStopped=$true }
    }
  }
  if ($chatNodesStopped) {
    Write-Host 'Waiting 31 seconds for stopped chat nodes to release their Redis leases...'
    Start-Sleep -Seconds 31
  }
  if ($ClientOnly) {
    Start-Project -Configuration Debug -NoClient
  } else {
    Start-Project -Configuration Debug -NoClient -SkipServer $Server
    Write-Host "$Server is reserved for Visual Studio. Select Debug | x64 and press F5."
    Start-Process 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.exe' -ArgumentList "`"$root\build\server\CppChatServers.slnx`"" -WorkingDirectory "$root\build\server"
  }
  $env:TEMP='D:\Dev\tmp'
  $env:TMP=$env:TEMP
  Start-Process 'D:\Qt\Tools\QtCreator\bin\qtcreator.exe' -ArgumentList '-client',"`"$root\CppChat\CMakeLists.txt`"" -WorkingDirectory "$root\CppChat"
}

switch ($Action) {
  'log-window' { Watch-ServerLogs; break }
  'start' { Start-Project -Configuration $Configuration -NoClient:$NoClient -InfrastructureOnly:$InfrastructureOnly; break }
  'stop' { Stop-Project; break }
  'build' { Build-Project; break }
  'debug' { Open-DebugEnvironment; break }
  'debug-client' { Open-DebugEnvironment -ClientOnly; break }
  'sync' { Sync-Configuration; break }
  'help' {
    Write-Host 'Usage: CppChat.cmd start|stop|build|debug|debug-client|sync|help [options]'
    Write-Host 'Build: -Configuration Debug|Release -Target All|Server|Client|GateServer|StatusServer|ChatServer|ChatServer2|ResourceServer -Jobs 8'
    Write-Host 'Dependencies: build -InstallDependencies (requires local proxy 127.0.0.1:7897)'
    Write-Host 'Debug: debug -Server GateServer|StatusServer|ChatServer|ChatServer2|ResourceServer'
    Write-Host 'Start: -Configuration Debug|Release -NoClient -InfrastructureOnly'
    Write-Host 'Double-click CppChat.cmd for the menu. MySQL84 is left running when stopping.'
    break
  }
}
