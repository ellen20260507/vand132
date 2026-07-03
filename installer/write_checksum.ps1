$ErrorActionPreference = 'Stop'
$OutputDir = Join-Path $PSScriptRoot 'output'
$InfoPath = Join-Path $OutputDir 'checksum.txt'

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('ESD-1000-B release checksum (vand1 3-2)')
[void]$sb.AppendLine('Generated: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
[void]$sb.AppendLine('')
[void]$sb.AppendLine('If setup.exe shows corrupted, the file was damaged during transfer.')
[void]$sb.AppendLine('Compare file size and SHA256 on the client PC. They must match exactly.')
[void]$sb.AppendLine('Prefer sending ESD-1000-B_Portable.zip via WeChat instead of setup.exe.')
[void]$sb.AppendLine('')

foreach ($name in @('ESD-1000-B_Setup.exe', 'ESD-1000-B_Portable.zip')) {
    $path = Join-Path $OutputDir $name
    if (Test-Path $path) {
        $item = Get-Item $path
        $hash = (Get-FileHash $path -Algorithm SHA256).Hash
        [void]$sb.AppendLine("File: $name")
        [void]$sb.AppendLine("SizeBytes: $($item.Length)")
        [void]$sb.AppendLine("SizeMB: $([math]::Round($item.Length / 1MB, 2))")
        [void]$sb.AppendLine("SHA256: $hash")
        [void]$sb.AppendLine('')
    }
}

[System.IO.File]::WriteAllText($InfoPath, $sb.ToString(), [System.Text.UTF8Encoding]::new($false))
Write-Host "Checksum file: $InfoPath"
