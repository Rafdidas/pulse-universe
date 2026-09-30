<#
.SYNOPSIS
  Builds the Pulse Universe release zip (release design, D71).

.DESCRIPTION
  1. builds the web frontend, 2. builds the engine with a static MSVC runtime and static Boost,
  3. checks that the exe has no Boost / Visual C++ runtime dependency, 4. assembles the zip and its
  SHA256. Used locally and by the GitHub release workflow, so both give the same result.

.PARAMETER Version
  Text used in the file name, e.g. 0.1.0 (the workflow passes the tag without the leading v).

.PARAMETER OutDir
  Where the zip and the .sha256 are written. Default: dist-release under the repository root.

.PARAMETER SkipNpmCi
  Skip "npm ci" (use the node_modules already there). Faster for local runs.
#>
[CmdletBinding()]
param(
    [string]$Version = 'dev',
    [string]$OutDir = '',
    [switch]$SkipNpmCi
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
if ($OutDir -eq '') { $OutDir = Join-Path $root 'dist-release' }
$OutDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir)
$name = "pulse-universe-v$Version-win-x64"

function Invoke-Native {
    param([string]$Description, [scriptblock]$Command)
    Write-Host "==> $Description"
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "$Description failed with exit code $LASTEXITCODE" }
}

# --- tools ---------------------------------------------------------------------------------
$vcpkgRoot = $env:VCPKG_ROOT
if (-not $vcpkgRoot) { $vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT }
if (-not $vcpkgRoot) { $vcpkgRoot = 'C:/vcpkg' }
if (-not (Test-Path (Join-Path $vcpkgRoot 'scripts/buildsystems/vcpkg.cmake'))) {
    throw "vcpkg not found at $vcpkgRoot (set VCPKG_ROOT)"
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $cmakeBin = 'C:\Program Files\CMake\bin'
    if (Test-Path $cmakeBin) { $env:PATH = "$cmakeBin;$env:PATH" } else { throw 'cmake not found on PATH' }
}

function Find-Dumpbin {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found (Visual Studio Build Tools missing?)' }
    $install = & $vswhere -latest -products * -property installationPath
    $found = Get-ChildItem (Join-Path $install 'VC\Tools\MSVC') -Directory |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' } |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $found) { throw 'dumpbin.exe not found' }
    return $found
}

# --- 1. web --------------------------------------------------------------------------------
Push-Location (Join-Path $root 'web')
try {
    if (-not $SkipNpmCi) { Invoke-Native 'npm ci' { npm ci } }
    Invoke-Native 'npm run build' { npm run build }
} finally { Pop-Location }
$webDist = Join-Path $root 'web\dist'
if (-not (Test-Path (Join-Path $webDist 'index.html'))) { throw 'web/dist/index.html is missing' }

# --- 2. engine (static runtime, static Boost) ----------------------------------------------
$engineDir = Join-Path $root 'engine'
$buildDir = Join-Path $engineDir 'build-release'
Invoke-Native 'cmake configure (x64-windows-static)' {
    cmake -S $engineDir -B $buildDir -G 'Visual Studio 17 2022' -A x64 `
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake" `
        -DVCPKG_TARGET_TRIPLET=x64-windows-static `
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
}
Invoke-Native 'cmake build (Release)' {
    cmake --build $buildDir --config Release
}
$exe = Join-Path $buildDir 'Release\pulse-engine.exe'
if (-not (Test-Path $exe)) { throw "engine exe missing: $exe" }

# --- 3. the exe must not depend on Boost or the Visual C++ runtime -------------------------
$dumpbin = Find-Dumpbin
$dependents = & $dumpbin /dependents $exe | Out-String
if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed' }
$bad = @($dependents -split "`r?`n" | Where-Object { $_ -match '(?i)boost|vcruntime|msvcp|vcomp|concrt' })
if ($bad.Count -gt 0) {
    throw "pulse-engine.exe still depends on: $($bad -join ', ') (static link failed)"
}
Write-Host '==> static link check passed'

# --- 4. assemble ---------------------------------------------------------------------------
$stageRoot = Join-Path $OutDir 'stage'
$stage = Join-Path $stageRoot $name
if (Test-Path $stageRoot) { Remove-Item $stageRoot -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
Copy-Item $webDist (Join-Path $stage 'web') -Recurse
Copy-Item (Join-Path $PSScriptRoot 'release\*') $stage

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
# The zip holds one top-level folder, so "Extract all" never spills files into the current directory.
# Entry names are written with forward slashes by hand: Windows PowerShell 5.1's
# CreateFromDirectory writes backslashes, which other unzip tools misread.
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $prefixLength = (Resolve-Path $stageRoot).Path.TrimEnd('\').Length + 1
    Get-ChildItem $stageRoot -Recurse -File | ForEach-Object {
        $entryName = $_.FullName.Substring($prefixLength).Replace('\', '/')
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $_.FullName, $entryName, [System.IO.Compression.CompressionLevel]::Optimal)
    }
} finally { $archive.Dispose() }
Remove-Item $stageRoot -Recurse -Force

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $name.zip" | Set-Content -Path "$zip.sha256" -Encoding ascii

$size = [math]::Round((Get-Item $zip).Length / 1MB, 2)
Write-Host "==> $zip ($size MB)"
Write-Host "==> sha256 $hash"
