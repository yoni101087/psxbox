# Creates a self-signed code signing certificate for sideloading PS Xbox (Dev Mode).
# The subject must match Publisher in app\Package.appxmanifest.
param(
    [string]$Password = "psxbox"
)

$ErrorActionPreference = "Stop"
$out = Join-Path $PSScriptRoot "..\app\PsXbox_TemporaryKey.pfx"

$cert = New-SelfSignedCertificate -Type Custom -Subject "CN=PsXboxDev" `
    -KeyUsage DigitalSignature -FriendlyName "PsXbox sideload" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")

$secure = ConvertTo-SecureString -String $Password -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath $out -Password $secure | Out-Null

Write-Host "Created $out"
Write-Host "Build with: -p:PackageCertificatePassword=$Password"
