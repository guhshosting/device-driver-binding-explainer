param([string]$BuildDirectory = 'build-release', [string]$OutputDirectory = 'dist')
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$configPath = Join-Path (Resolve-Path $BuildDirectory) 'CPackConfig.cmake'
$outputPath = (Resolve-Path $OutputDirectory).Path
cpack --config $configPath -G ZIP -C Release -B $outputPath
if ($LASTEXITCODE) { throw 'Packaging failed' }
$archives = Get-ChildItem -LiteralPath $outputPath -Filter 'device-driver-binding-explainer-1.0.0-windows-*.zip'
if (!$archives) { throw 'Release ZIP was not created' }
$lines = foreach ($archive in $archives) {
    $hash = Get-FileHash -LiteralPath $archive.FullName -Algorithm SHA256
    "$($hash.Hash.ToLowerInvariant())  $($archive.Name)"
}
$lines | Set-Content -LiteralPath (Join-Path $outputPath 'SHA256SUMS.txt') -Encoding utf8NoBOM
