param(
  [Parameter(Mandatory=$true)][string]$Artifact,
  [Parameter(Mandatory=$true)][string]$SigningConfig
)
$ErrorActionPreference = 'Stop'
$Artifact = (Resolve-Path -LiteralPath $Artifact).Path
$config = Get-Content -LiteralPath $SigningConfig -Raw -Encoding UTF8 | ConvertFrom-Json
if ($config.CertificateThumbprint -notmatch '^[a-fA-F0-9]{40}$' -or
    $config.TimestampUrl -notmatch '^https://[^\s]+$') { throw 'Invalid code-signing configuration.' }
$certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$($config.CertificateThumbprint)"
if (-not $certificate.HasPrivateKey -or $certificate.NotAfter -le (Get-Date) -or
    $certificate.NotBefore -gt (Get-Date) -or
    -not ($certificate.EnhancedKeyUsageList | Where-Object { $_.ObjectId -eq '1.3.6.1.5.5.7.3.3' })) {
  throw 'A valid code-signing certificate with its private key is required.'
}
$tool = (Resolve-Path -LiteralPath $config.SignTool).Path
# Certificate-store signing also supports hardware/cloud-backed keys through their Windows provider.
# No PFX passwords or private keys are passed on the command line or copied into the payload.
& $tool sign /sha1 $config.CertificateThumbprint /s My /fd SHA256 /tr $config.TimestampUrl /td SHA256 $Artifact
if ($LASTEXITCODE -ne 0) { throw 'Code-signing tool failed.' }
$signature = Get-AuthenticodeSignature -LiteralPath $Artifact
if ($signature.Status -ne 'Valid' -or
    $signature.SignerCertificate.Thumbprint -ne $config.CertificateThumbprint) {
  throw 'Signature trust or publisher verification failed.'
}
Write-Output "Signed and verified: $([IO.Path]::GetFileName($Artifact))"
