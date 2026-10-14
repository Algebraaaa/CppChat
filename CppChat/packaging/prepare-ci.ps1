param([Parameter(Mandatory=$true)][string]$QtRoot)
$ErrorActionPreference='Stop'
$repositoryRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$tools=Join-Path $repositoryRoot 'build\dependencies\inno-setup-6.7.3'
New-Item -ItemType Directory -Path $tools -Force | Out-Null
$download=Join-Path $tools 'innosetup-6.7.3.exe'
Invoke-WebRequest 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe' -OutFile $download
$signature=Get-AuthenticodeSignature -LiteralPath $download
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Pyrsys B\.V\.') {
  throw 'Inno Setup signature verification failed.'
}
$process=Start-Process -FilePath $download -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/DIR=`"$tools\installed`"") -WindowStyle Hidden -Wait -PassThru
if($process.ExitCode -ne 0){throw 'Inno Setup installation failed.'}
$licenses=Join-Path $repositoryRoot 'build\dependencies\qt-licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
foreach($name in @('LGPL-3.0-only.txt','GPL-3.0-only.txt','Qt-GPL-exception-1.0.txt')) {
  Invoke-WebRequest "https://raw.githubusercontent.com/qt/qtbase/v6.12.0/LICENSES/$name" -OutFile (Join-Path $licenses $name)
}
"CPPCHAT_ISCC=$tools\installed\ISCC.exe" | Out-File -LiteralPath $env:GITHUB_ENV -Encoding utf8 -Append
"CPPCHAT_QT_LICENSES=$licenses" | Out-File -LiteralPath $env:GITHUB_ENV -Encoding utf8 -Append
