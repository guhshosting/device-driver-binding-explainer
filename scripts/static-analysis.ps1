param([string]$BuildDirectory = 'build-release')
$ErrorActionPreference = 'Stop'
$toolchainRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231'
$sdkInclude = 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0'
$includePaths = @("$toolchainRoot\include", "$sdkInclude\ucrt", "$sdkInclude\shared", "$sdkInclude\um")
$tidyArgs = @('-p', $BuildDirectory, '--warnings-as-errors=*', '--extra-arg=--driver-mode=cl')
foreach ($includePath in $includePaths) { $tidyArgs += "--extra-arg=/imsvc$includePath" }
foreach ($sourceFile in @('src/core.cpp','src/windows.cpp','src/main.cpp','tests/tests.cpp')) {
    & 'C:\Program Files\LLVM\bin\clang-tidy.exe' $sourceFile @tidyArgs
    if ($LASTEXITCODE) { throw "clang-tidy failed: $sourceFile" }
}
& 'C:\Program Files\Cppcheck\cppcheck.exe' --platform=win64 --std=c++17 --enable=warning,style,performance,portability --error-exitcode=1 --inline-suppr --suppress=missingIncludeSystem -Isrc src tests
if ($LASTEXITCODE) { throw 'Cppcheck failed' }
