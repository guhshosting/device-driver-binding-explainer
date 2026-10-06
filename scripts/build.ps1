param([string]$BuildDirectory = 'build-release', [switch]$Analyze)
$ErrorActionPreference = 'Stop'
$compilerRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231'
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVersion = '10.0.26100.0'
$ninjaDirectory = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
if (!(Test-Path "$compilerRoot\bin\Hostx64\x64\cl.exe")) { throw 'Adjust compilerRoot and SDK paths for your installation, or configure CMake from a VS developer terminal.' }
$env:PATH = "$compilerRoot\bin\Hostx64\x64;$sdkRoot\bin\$sdkVersion\x64;$ninjaDirectory;" + $env:PATH
$env:INCLUDE = "$compilerRoot\include;$sdkRoot\Include\$sdkVersion\ucrt;$sdkRoot\Include\$sdkVersion\shared;$sdkRoot\Include\$sdkVersion\um"
$env:LIB = "$compilerRoot\lib\x64;$sdkRoot\Lib\$sdkVersion\ucrt\x64;$sdkRoot\Lib\$sdkVersion\um\x64"
$analysisFlag = if ($Analyze) { 'ON' } else { 'OFF' }
cmake -S . -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release "-DDDBE_ANALYZE=$analysisFlag"
if ($LASTEXITCODE) { throw 'Configuration failed' }
cmake --build $BuildDirectory
if ($LASTEXITCODE) { throw 'Build failed' }
ctest --test-dir $BuildDirectory --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
