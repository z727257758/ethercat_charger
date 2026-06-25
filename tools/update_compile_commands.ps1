param(
    [string[]]$BuildDirs = @(
        "build\ninja\bootuser",
        "build\ninja\user_app"
    ),
    [string]$Output = "compile_commands.json"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$entries = New-Object System.Collections.Generic.List[object]
$seen = @{}

foreach ($buildDir in $BuildDirs) {
    $path = Join-Path $root (Join-Path $buildDir "compile_commands.json")
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Warning "Skip missing compile database: $path"
        continue
    }

    $items = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    foreach ($item in $items) {
        $key = "$($item.file)|$($item.output)"
        if ($seen.ContainsKey($key)) {
            continue
        }

        $seen[$key] = $true
        $entries.Add($item)
    }
}

if ($entries.Count -eq 0) {
    throw "No compile commands were found. Run .\tools\build.ps1 first."
}

$outputPath = Join-Path $root $Output
$entries | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outputPath -Encoding utf8
Write-Host "Wrote $($entries.Count) entries to $outputPath"
