# Prepare installer staging folder from deploy_v2 + this project's build output
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ProjectRoot = Split-Path $PSScriptRoot -Parent
$DeploySource = 'D:\QT\object\deploy_v2'
$ReleaseDir = Join-Path $ProjectRoot 'release'
$StagingDir = Join-Path $ProjectRoot 'installer\staging'

if (-not (Test-Path (Join-Path $ReleaseDir 'vand1.exe'))) {
    throw "Built executable not found: $(Join-Path $ReleaseDir 'vand1.exe')"
}

# If deploy_v2 is missing, reuse current staging as the base (backup first).
$tempDeploy = $null
if (-not (Test-Path $DeploySource)) {
    if (-not (Test-Path $StagingDir)) {
        throw "Deploy source not found: $DeploySource (and no existing staging to reuse)"
    }
    $tempDeploy = Join-Path $env:TEMP ('vand1_staging_base_' + [guid]::NewGuid().ToString('N'))
    Copy-Item $StagingDir $tempDeploy -Recurse -Force
    $DeploySource = $tempDeploy
    Write-Host "deploy_v2 missing, reusing current staging as base" -ForegroundColor Yellow
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

function Copy-FirstExisting {
    param(
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string[]]$Candidates,
        [string]$Label
    )
    foreach ($src in $Candidates) {
        if ($src -and (Test-Path -LiteralPath $src)) {
            $destDir = Split-Path $Destination -Parent
            if ($destDir -and -not (Test-Path $destDir)) {
                New-Item -ItemType Directory -Path $destDir -Force | Out-Null
            }
            Copy-Item -LiteralPath $src -Destination $Destination -Force
            if ($Label) {
                Write-Host ("Applied {0} <= {1}" -f $Label, $src) -ForegroundColor DarkCyan
            }
            return $true
        }
    }
    return $false
}

# MySQL QMYSQL runtime deps: libmysql needs OpenSSL 3 (missing => Driver not loaded)
$mysqlBins = @(
    'C:\Program Files\MySQL\MySQL Server 8.0\bin',
    'C:\Program Files\MySQL\MySQL Server 8.4\bin',
    'C:\Program Files\MySQL\MySQL Server 5.7\bin'
)
$mysqlLibs = @(
    'C:\Program Files\MySQL\MySQL Server 8.0\lib',
    'C:\Program Files\MySQL\MySQL Server 8.4\lib',
    'C:\Program Files\MySQL\MySQL Server 5.7\lib'
)

$libmysqlCandidates = New-Object System.Collections.Generic.List[string]
$libmysqlCandidates.Add((Join-Path $ReleaseDir 'libmysql.dll'))
foreach ($lib in $mysqlLibs) { $libmysqlCandidates.Add((Join-Path $lib 'libmysql.dll')) }
$libmysqlCandidates.Add((Join-Path $StagingDir 'libmysql.dll'))
if (-not (Copy-FirstExisting -Destination (Join-Path $StagingDir 'libmysql.dll') -Candidates $libmysqlCandidates.ToArray() -Label 'libmysql.dll')) {
    Write-Warning 'Missing required MySQL runtime DLL for packaging: libmysql.dll'
}

$libsslCandidates = New-Object System.Collections.Generic.List[string]
$libsslCandidates.Add((Join-Path $ReleaseDir 'libssl-3-x64.dll'))
foreach ($bin in $mysqlBins) { $libsslCandidates.Add((Join-Path $bin 'libssl-3-x64.dll')) }
if (-not (Copy-FirstExisting -Destination (Join-Path $StagingDir 'libssl-3-x64.dll') -Candidates $libsslCandidates.ToArray() -Label 'libssl-3-x64.dll')) {
    Write-Warning 'Missing required MySQL runtime DLL for packaging: libssl-3-x64.dll'
}

$libcryptoCandidates = New-Object System.Collections.Generic.List[string]
$libcryptoCandidates.Add((Join-Path $ReleaseDir 'libcrypto-3-x64.dll'))
foreach ($bin in $mysqlBins) { $libcryptoCandidates.Add((Join-Path $bin 'libcrypto-3-x64.dll')) }
if (-not (Copy-FirstExisting -Destination (Join-Path $StagingDir 'libcrypto-3-x64.dll') -Candidates $libcryptoCandidates.ToArray() -Label 'libcrypto-3-x64.dll')) {
    Write-Warning 'Missing required MySQL runtime DLL for packaging: libcrypto-3-x64.dll'
}

# Ensure qsqlmysql is under plugins/sqldrivers
$qsqlDst = Join-Path $StagingDir 'plugins\sqldrivers\qsqlmysql.dll'
$qsqlCandidates = @(
    (Join-Path $ReleaseDir 'plugins\sqldrivers\qsqlmysql.dll'),
    (Join-Path $StagingDir 'plugins\sqldrivers\qsqlmysql.dll'),
    (Join-Path $StagingDir 'qsqlmysql.dll')
)
[void](Copy-FirstExisting -Destination $qsqlDst -Candidates $qsqlCandidates -Label 'qsqlmysql.dll')

$staticDir = Join-Path $StagingDir 'static'
New-Item -ItemType Directory -Path $staticDir -Force | Out-Null

$indexCandidates = @(
    (Join-Path $ReleaseDir 'static\index.html'),
    (Join-Path $ProjectRoot 'static\index.html'),
    (Join-Path $StagingDir 'static\index.html')
)
$indexCopied = $false
foreach ($candidate in $indexCandidates) {
    if (Test-Path -LiteralPath $candidate) {
        Copy-Item -LiteralPath $candidate -Destination (Join-Path $staticDir 'index.html') -Force
        $indexCopied = $true
        break
    }
}
if (-not $indexCopied) {
    throw 'index.html not found in release/static or static'
}

$echartsCandidates = @(
    (Join-Path $ReleaseDir 'static\echarts.min.js'),
    (Join-Path $DeploySource 'static\echarts.min.js'),
    (Join-Path $StagingDir 'static\echarts.min.js')
)
foreach ($candidate in $echartsCandidates) {
    if (Test-Path -LiteralPath $candidate) {
        Copy-Item -LiteralPath $candidate -Destination (Join-Path $staticDir 'echarts.min.js') -Force
        break
    }
}

# Overlay project-specific configs from release
foreach ($cfg in @('config.json', 'device_backup.json', 'poll_config.txt', 'server.ini', 'mysql_config.ini', 'device_modify.ini')) {
    $src = Join-Path $ReleaseDir $cfg
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src -Destination (Join-Path $StagingDir $cfg) -Force
        Write-Host "Applied project config: $cfg" -ForegroundColor DarkCyan
    }
}

Get-ChildItem $StagingDir -Recurse -Include $excludePatterns -File -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

if ($tempDeploy -and (Test-Path $tempDeploy)) {
    Remove-Item $tempDeploy -Recurse -Force -ErrorAction SilentlyContinue
}

$exe = Get-Item (Join-Path $StagingDir 'vand1.exe')
$sizeMb = [math]::Round((Get-ChildItem $StagingDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)

Write-Host "Staging ready: $StagingDir" -ForegroundColor Green
Write-Host "Executable: $($exe.FullName) ($([math]::Round($exe.Length / 1KB)) KB, $($exe.LastWriteTime))" -ForegroundColor Cyan
Write-Host "Package size: about $sizeMb MB" -ForegroundColor Cyan
