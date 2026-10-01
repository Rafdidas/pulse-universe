<#
.SYNOPSIS
  Builds the Pulse Universe release zip (release design, D71).

.DESCRIPTION
  1. builds the web frontend and packs it into web.pak, 2. builds the engine with a static MSVC
  runtime and static Boost, the pak embedded in the exe, 3. checks that the exe has no Boost / Visual
  C++ runtime dependency, 4. starts a copy of the exe alone and checks it serves the embedded
  frontend, 5. assembles the zip and its SHA256, 6. writes the bare exe and its SHA256. Used locally
  and by the GitHub release workflow, so both give the same result.

  The work is split in two phases so a signing service can sit between them (code signing design,
  D82). Build runs steps 1-4 and leaves OutDir\unsigned\pulse-engine.exe. Assemble takes the exe to
  ship from -ExePath (for example the signed one), checks it again, and runs steps 5-6. The default,
  All, runs both back to back with the unsigned exe.

.PARAMETER Version
  Text used in the file name, e.g. 0.1.0 (the workflow passes the tag without the leading v). When it
  looks like x.y.z it is also stored in the exe's file properties; otherwise the exe says 0.0.0.

.PARAMETER OutDir
  Where the zip, the bare pulse-engine.exe and their two .sha256 files are written. Default:
  dist-release under the repository root.

.PARAMETER SkipNpmCi
  Skip "npm ci" (use the node_modules already there). Faster for local runs.

.PARAMETER Phase
  All (default), Build or Assemble. Assemble needs web/dist from a Build in the same checkout.

.PARAMETER ExePath
  Assemble only: the pulse-engine.exe to put into the zip and to publish (required).

.PARAMETER RequireSignature
  Assemble only: fail unless the exe carries a valid Authenticode signature.
#>
[CmdletBinding()]
param(
    [string]$Version = 'dev',
    [string]$OutDir = '',
    [switch]$SkipNpmCi,
    [ValidateSet('All', 'Build', 'Assemble')][string]$Phase = 'All',
    [string]$ExePath = '',
    [switch]$RequireSignature
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
if ($OutDir -eq '') { $OutDir = Join-Path $root 'dist-release' }
$OutDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir)
$name = "pulse-universe-v$Version-win-x64"
$webDist = Join-Path $root 'web\dist'
$engineDir = Join-Path $root 'engine'
$buildDir = Join-Path $engineDir 'build-release'
$unsignedExe = Join-Path $OutDir 'unsigned\pulse-engine.exe'

if ($Phase -eq 'Assemble') {
    if ($ExePath -eq '') { throw '-Phase Assemble needs -ExePath (the pulse-engine.exe to ship)' }
    $ExePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ExePath)
    if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) { throw "-ExePath does not exist: $ExePath" }
} else {
    if ($ExePath -ne '') { throw '-ExePath is only for -Phase Assemble' }
    if ($RequireSignature) { throw '-RequireSignature is only for -Phase Assemble' }
}

function Invoke-Native {
    param([string]$Description, [scriptblock]$Command)
    Write-Host "==> $Description"
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "$Description failed with exit code $LASTEXITCODE" }
}

# The exe alone must serve the frontend. Run before anything is written to the release files, so a
# failed probe leaves none. Copy the exe to an empty folder (no web/ next to it) and ask it for the
# embedded assets. The no-argument launch would open a browser, so the script uses
# --serve --embedded-web instead. "--mapping estimated" keeps the probe from starting ETW (no
# administrator rights needed).
function Test-EmbeddedWeb {
    param([string]$Exe)
    $probeDir = Join-Path ([System.IO.Path]::GetTempPath()) ("pulse-probe-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $probeDir | Out-Null
    $listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = $listener.LocalEndpoint.Port
    $listener.Stop()
    $probeExe = Join-Path $probeDir 'pulse-engine.exe'
    Copy-Item -LiteralPath $Exe $probeExe
    $probe = Start-Process -FilePath $probeExe `
        -ArgumentList '--serve', '--embedded-web', '--port', $port, '--mapping', 'estimated' `
        -WorkingDirectory $probeDir -WindowStyle Hidden -PassThru
    try {
        $script = Get-ChildItem (Join-Path $webDist 'assets') -Filter '*.js' | Select-Object -First 1
        $checks = @('/', "/assets/$($script.Name)", '/some/spa/route')
        foreach ($path in $checks) {
            $response = $null
            for ($i = 0; $i -lt 50 -and $null -eq $response; $i++) {
                if ($probe.HasExited) { throw "the probe exe exited early with code $($probe.ExitCode)" }
                try { $response = Invoke-WebRequest "http://127.0.0.1:$port$path" -UseBasicParsing -TimeoutSec 2 }
                catch { Start-Sleep -Milliseconds 200 }
            }
            if ($null -eq $response -or $response.StatusCode -ne 200) { throw "embedded web: GET $path did not return 200" }
        }
        $served = (Invoke-WebRequest "http://127.0.0.1:$port/assets/$($script.Name)" -UseBasicParsing).RawContentLength
        if ($served -ne $script.Length) { throw "embedded $($script.Name) is $served bytes, expected $($script.Length)" }
        Write-Host '==> embedded web check passed'
    } finally {
        if (-not $probe.HasExited) { Stop-Process -Id $probe.Id -Force }
        $probe.WaitForExit()
        Remove-Item $probeDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($Phase -ne 'Assemble') {
    # --- tools -----------------------------------------------------------------------------
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

    # --- 1. web ----------------------------------------------------------------------------
    Push-Location (Join-Path $root 'web')
    try {
        if (-not $SkipNpmCi) { Invoke-Native 'npm ci' { npm ci } }
        Invoke-Native 'npm run build' { npm run build }
    } finally { Pop-Location }
    if (-not (Test-Path (Join-Path $webDist 'index.html'))) { throw 'web/dist/index.html is missing' }

    # --- 2. pack the frontend so the engine can embed it -----------------------------------
    # The pak lives in a subfolder of the build folder (not in dist-release): CMake's cache keeps the
    # path, so the file must stay there for later "cmake --build" runs. A subfolder, because the engine's
    # CMakeLists copies it to <build>/web.pak.
    $pakDir = Join-Path $buildDir 'pak'
    New-Item -ItemType Directory -Force $pakDir | Out-Null
    $pak = Join-Path $pakDir 'web.pak'
    & (Join-Path $PSScriptRoot 'make-pak.ps1') -WebDist $webDist -Out $pak

    # --- 3. engine (static runtime, static Boost) ------------------------------------------
    # The version goes into the exe's file properties (a signing service needs one); a name like "dev"
    # is not a version, so the exe then says 0.0.0.
    $exeVersion = '0.0.0'
    if ($Version -match '^\d+\.\d+\.\d+$') { $exeVersion = $Version }
    Invoke-Native 'cmake configure (x64-windows-static)' {
        cmake -S $engineDir -B $buildDir -G 'Visual Studio 17 2022' -A x64 `
            "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake" `
            -DVCPKG_TARGET_TRIPLET=x64-windows-static `
            -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
            "-DPULSE_VERSION=$exeVersion" `
            "-DPULSE_WEB_PAK=$($pak.Replace('\', '/'))"
    }
    Invoke-Native 'cmake build (Release)' {
        cmake --build $buildDir --config Release
    }
    $exe = Join-Path $buildDir 'Release\pulse-engine.exe'
    if (-not (Test-Path $exe)) { throw "engine exe missing: $exe" }

    # --- 4. the exe must not depend on Boost or the Visual C++ runtime ---------------------
    $dumpbin = Find-Dumpbin
    $dependents = & $dumpbin /dependents $exe | Out-String
    if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed' }
    $bad = @($dependents -split "`r?`n" | Where-Object { $_ -match '(?i)boost|vcruntime|msvcp|vcomp|concrt' })
    if ($bad.Count -gt 0) {
        throw "pulse-engine.exe still depends on: $($bad -join ', ') (static link failed)"
    }
    Write-Host '==> static link check passed'

    Test-EmbeddedWeb $exe

    New-Item -ItemType Directory -Force (Split-Path -Parent $unsignedExe) | Out-Null
    Copy-Item $exe $unsignedExe -Force
    Write-Host "==> $unsignedExe (to be signed, or shipped as it is)"
}

if ($Phase -eq 'Build') { return }

# --- Assemble --------------------------------------------------------------------------------
if ($Phase -eq 'All') { $ExePath = $unsignedExe }

if ($RequireSignature) {
    $signature = Get-AuthenticodeSignature -LiteralPath $ExePath
    if ($signature.Status -ne 'Valid') {
        throw "$ExePath is not validly signed (status: $($signature.Status)); refusing to publish it"
    }
    Write-Host "==> signature valid: $($signature.SignerCertificate.Subject)"
}

# Signing must not break the embedded frontend: check the exe that will actually ship.
Test-EmbeddedWeb $ExePath

# --- 5. assemble the zip ---------------------------------------------------------------------
New-Item -ItemType Directory -Force $OutDir | Out-Null
$stageRoot = Join-Path $OutDir 'stage'
$stage = Join-Path $stageRoot $name
if (Test-Path $stageRoot) { Remove-Item $stageRoot -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item -LiteralPath $ExePath (Join-Path $stage 'pulse-engine.exe')
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

# --- 6. the bare exe and its checksum --------------------------------------------------------
$bareExe = Join-Path $OutDir 'pulse-engine.exe'
Copy-Item -LiteralPath $ExePath $bareExe -Force
$exeHash = (Get-FileHash $bareExe -Algorithm SHA256).Hash.ToLower()
"$exeHash  pulse-engine.exe" | Set-Content -Path "$bareExe.sha256" -Encoding ascii
Write-Host "==> $bareExe ($([math]::Round((Get-Item $bareExe).Length / 1MB, 2)) MB)"
Write-Host "==> sha256 $exeHash"
