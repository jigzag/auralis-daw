$ErrorActionPreference = 'Stop'
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
} else {
    $cmakePath = 'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath $cmakePath)) { throw 'CMake 3.22 or newer is required.' }
Push-Location $PSScriptRoot
try {
    & $cmakePath -S . -B build -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmakePath --build build --config Release --parallel 1
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    Copy-Item -LiteralPath build/TestGain_artefacts/Release/VST3/TestGain.vst3 -Destination build/Auralis_artefacts/Release/TestGain.vst3 -Recurse -Force
    Copy-Item -LiteralPath build/TestTone_artefacts/Release/VST3/TestTone.vst3 -Destination build/Auralis_artefacts/Release/TestTone.vst3 -Recurse -Force
    Write-Output 'Built: build/Auralis_artefacts/Release/Auralis.exe'
} finally {
    Pop-Location
}
