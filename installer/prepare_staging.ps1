# Prepare installer staging folder from deploy_v2 + this project's build output
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ProjectRoot = Split-Path $PSScriptRoot -Parent
$DeploySource = 'D:\QT\object\deploy_v2'
$ReleaseDir = Join-Path $ProjectRoot 'release'
$StagingDir = Join-Path $ProjectRoot 'installer\staging'

if (-not (Test-Path $DeploySource)) {
    throw "Deploy source not found: $DeploySource"
}
if (-not (Test-Path (Join-Path $ReleaseDir 'vand1.exe'))) {
    throw "Built executable not found: $(Join-Path $ReleaseDir 'vand1.exe')"
}

Write-Host 'Preparing installer staging folder (vand1 3-2)...' -ForegroundColor Green

if (Test-Path $StagingDir) {
    Remove-Item $StagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StagingDir | Out-Null

$excludeNames = @('vand.exe', 'err.txt', 'MS-518.lnk')
$excludePatterns = @('*.log')

Get-ChildItem $DeploySource -Force | ForEach-Object {
    if ($excludeNames -contains $_.Name) {
        return
    }

    if ($_.PSIsContainer -and $_.Name -eq 'logs') {
        New-Item -ItemType Directory -Path (Join-Path $StagingDir 'logs') | Out-Null
        return
    }

    Copy-Item $_.FullName -Destination $StagingDir -Recurse -Force
}

Copy-Item (Join-Path $ReleaseDir 'vand1.exe') (Join-Path $StagingDir 'vand1.exe') -Force

$staticDir = Join-Path $StagingDir 'static'
New-Item -ItemType Directory -Path $staticDir -Force | Out-Null

$indexCandidates = @(
    (Join-Path $ReleaseDir 'static\index.html'),
    (Join-Path $ProjectRoot 'static\index.html')
)
$indexCopied = $false
foreach ($candidate in $indexCandidates) {
    if (Test-Path $candidate) {
        Copy-Item $candidate (Join-Path $staticDir 'index.html') -Force
        $indexCopied = $true
        break
    }
}
if (-not $indexCopied) {
    throw 'index.html not found in release/static or static'
}

$echartsCandidates = @(
    (Join-Path $ReleaseDir 'static\echarts.min.js'),
    (Join-Path $DeploySource 'static\echarts.min.js')
)
foreach ($candidate in $echartsCandidates) {
    if (Test-Path $candidate) {
        Copy-Item $candidate (Join-Path $staticDir 'echarts.min.js') -Force
        break
    }
}

# Overlay project-specific configs from release
foreach ($cfg in @('config.json', 'device_backup.json', 'poll_config.txt', 'server.ini', 'mysql_config.ini')) {
    $src = Join-Path $ReleaseDir $cfg
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $StagingDir $cfg) -Force
        Write-Host "Applied project config: $cfg" -ForegroundColor DarkCyan
    }
}

Get-ChildItem $StagingDir -Recurse -Include $excludePatterns -File -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

$exe = Get-Item (Join-Path $StagingDir 'vand1.exe')
$sizeMb = [math]::Round((Get-ChildItem $StagingDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)

Write-Host "Staging ready: $StagingDir" -ForegroundColor Green
Write-Host "Executable: $($exe.FullName) ($([math]::Round($exe.Length / 1KB)) KB, $($exe.LastWriteTime))" -ForegroundColor Cyan
Write-Host "Package size: about $sizeMb MB" -ForegroundColor Cyan
