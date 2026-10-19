<#
.NOTES
  cxmx-mod-lod-114d by cxmx-dev, CC BY-NC-SA 4.0

.SYNOPSIS
  Applies the cxmx-mod byte patches to your own copy of Game.exe (1.14d).

.DESCRIPTION
  Checks the SHA-256 of the target first and only writes to a stock file.
  Keeps a backup (<file>.orig), applies the patch in memory,
  and checks the SHA-256 of the result before it replaces the file.
  Close the game before you run it.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File patch\Apply-GameExePatch.ps1
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File patch\Apply-GameExePatch.ps1 -Check
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File patch\Apply-GameExePatch.ps1 -Undo
#>
[CmdletBinding()]
param(
  [string]$GameDir = '',
  [string]$PatchFile = '',
  [switch]$Check,
  [switch]$Undo
)

$ErrorActionPreference = 'Stop'
if (-not $GameDir) { $GameDir = Split-Path -Parent $PSScriptRoot }
if (-not $PatchFile) { $PatchFile = Join-Path $PSScriptRoot 'gameexe-patches.json' }

function Get-Sha256([byte[]]$Bytes) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try { return ([System.BitConverter]::ToString($sha.ComputeHash($Bytes)) -replace '-', '') }
  finally { $sha.Dispose() }
}

function ConvertFrom-Hex([string]$Hex) {
  $out = New-Object byte[] ($Hex.Length / 2)
  for ($i = 0; $i -lt $out.Length; $i++) { $out[$i] = [Convert]::ToByte($Hex.Substring($i * 2, 2), 16) }
  return ,$out
}

$spec = Get-Content -LiteralPath $PatchFile -Raw | ConvertFrom-Json
$target = Join-Path $GameDir $spec.target
$backup = $target + '.orig'
$stockHash = $spec.stock_sha256.ToUpperInvariant()
$expectHash = $spec.expect_sha256.ToUpperInvariant()

if (-not (Test-Path -LiteralPath $target)) { throw "Not found: $target. Run this from the mod folder or pass -GameDir." }

if ($Undo) {
  if (-not (Test-Path -LiteralPath $backup)) { throw "No backup found: $backup" }
  $orig = [System.IO.File]::ReadAllBytes($backup)
  if ((Get-Sha256 $orig) -ne $stockHash) { throw "The backup is not the stock file. Nothing changed." }
  [System.IO.File]::WriteAllBytes($target, $orig)
  Write-Host "Restored the stock $($spec.target) from $($spec.target).orig."
  exit 0
}

$bytes = [System.IO.File]::ReadAllBytes($target)
$hash = Get-Sha256 $bytes
if ($hash -eq $expectHash) { Write-Host "$($spec.target) is already patched. Nothing to do."; exit 0 }
if ($hash -ne $stockHash) {
  throw "$($spec.target) is not the expected stock $($spec.version) file (SHA-256 $hash). Nothing changed."
}

foreach ($p in $spec.patches) {
  $off = [Convert]::ToInt32($p.offset, 16)
  $new = ConvertFrom-Hex $p.new
  if (($off + $new.Length) -gt $bytes.Length) { throw "Bad patch entry at $($p.offset)." }
  [System.Array]::Copy($new, 0, $bytes, $off, $new.Length)
}

$result = Get-Sha256 $bytes
if ($result -ne $expectHash) { throw "Result SHA-256 $result does not match the expected $expectHash. Nothing changed." }

if ($Check) {
  Write-Host "Check OK: $($spec.patches.Count) entries give the expected result. Nothing written."
  exit 0
}

if (-not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $target -Destination $backup }
$tmp = $target + '.tmp'
[System.IO.File]::WriteAllBytes($tmp, $bytes)
Move-Item -LiteralPath $tmp -Destination $target -Force
Write-Host "Patched $($spec.target) ($($spec.patches.Count) entries). Backup: $($spec.target).orig"
