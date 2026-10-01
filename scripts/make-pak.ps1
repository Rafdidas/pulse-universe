<#
.SYNOPSIS
  Packs a built web folder (web/dist) into one web.pak file that the engine embeds.
.PARAMETER WebDist
  The folder to pack (every file under it, recursively).
.PARAMETER Out
  The pak file to write.
#>
param(
    [Parameter(Mandatory = $true)][string]$WebDist,
    [Parameter(Mandatory = $true)][string]$Out
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path -LiteralPath $WebDist).Path.TrimEnd('\')
$byPath = @{}
foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File) {
    $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
    $byPath['/' + $relative] = $file.FullName
}
if ($byPath.Count -eq 0) { throw "no files under $root" }

# Ascending ordinal order: the engine binary-searches the table and rejects anything else.
[string[]]$paths = @($byPath.Keys)
[Array]::Sort($paths, [System.StringComparer]::Ordinal)

$utf8 = New-Object System.Text.UTF8Encoding($false)
$pathBytes = @{}
$tableSize = 0
foreach ($path in $paths) {
    $bytes = $utf8.GetBytes($path)
    if ($bytes.Length -gt 65535) { throw "path too long: $path" }
    $pathBytes[$path] = $bytes
    $tableSize += 2 + $bytes.Length + 8 + 8
}

$stream = [System.IO.File]::Create($Out)
try {
    $writer = New-Object System.IO.BinaryWriter($stream)
    $writer.Write([byte[]][System.Text.Encoding]::ASCII.GetBytes("PLSPAK1`0"))
    $writer.Write([uint32]$paths.Count)

    $offset = [uint64](8 + 4 + $tableSize)
    foreach ($path in $paths) {
        $size = [uint64](New-Object System.IO.FileInfo($byPath[$path])).Length
        $writer.Write([uint16]$pathBytes[$path].Length)
        $writer.Write([byte[]]$pathBytes[$path])
        $writer.Write([uint64]$offset)
        $writer.Write([uint64]$size)
        $offset += $size
    }
    foreach ($path in $paths) {
        $writer.Write([byte[]][System.IO.File]::ReadAllBytes($byPath[$path]))
    }
    $writer.Flush()
} finally {
    $stream.Dispose()
}
Write-Host "==> $Out ($($paths.Count) files, $((Get-Item -LiteralPath $Out).Length) bytes)"
