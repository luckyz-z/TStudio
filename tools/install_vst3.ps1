param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string]$Destination = (Join-Path $env:CommonProgramFiles "VST3")
)

$ErrorActionPreference = "Stop"
$bundles = @(
    (Join-Path $BuildDir "TStudioLuaFX_artefacts\Release\VST3\TStudio Lua FX.vst3"),
    (Join-Path $BuildDir "TStudioLuaSynth_artefacts\Release\VST3\TStudio Lua Synth.vst3")
)

foreach ($bundle in $bundles) {
    if (-not (Test-Path $bundle)) {
        throw "Missing $bundle. Build TStudio first (cmake --build build --config Release)."
    }
}

try {
    New-Item -ItemType Directory -Force $Destination | Out-Null
    foreach ($bundle in $bundles) {
        $target = Join-Path $Destination (Split-Path $bundle -Leaf)
        if (Test-Path $target) { Remove-Item -Recurse -Force $target }
        Copy-Item -Recurse $bundle $target
        Write-Host "Installed $target"
    }
    Write-Host "Done. Rescan plugins in your DAW (FL Studio: Options > Manage plugins > Find more plugins)."
}
catch {
    Write-Host "Could not install: $($_.Exception.Message)"
    Write-Host "Writing to $Destination usually needs administrator rights. Run this script from an elevated PowerShell, or pass -Destination with a folder your DAW scans."
    exit 1
}
