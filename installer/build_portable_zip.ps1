$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$StagingDir = Join-Path $PSScriptRoot 'staging'
$OutputDir = Join-Path $PSScriptRoot 'output'
$ZipPath = Join-Path $OutputDir 'ESD-1000-B_Portable.zip'

& (Join-Path $PSScriptRoot 'prepare_staging.ps1')

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
if (Test-Path $ZipPath) {
    Remove-Item $ZipPath -Force
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($StagingDir, $ZipPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)

$zip = Get-Item $ZipPath
Write-Host "Portable ZIP ready: $($zip.FullName)" -ForegroundColor Green
Write-Host "Size: $([math]::Round($zip.Length / 1MB, 1)) MB" -ForegroundColor Cyan
