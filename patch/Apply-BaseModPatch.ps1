<#
.NOTES
  cxmx-mod-lod-114d by cxmx-dev, CC BY-NC-SA 4.0

.SYNOPSIS
  Applies the cxmx-mod byte patches to your own copy of BaseMod.dll (1.1.3.9).

.DESCRIPTION
  Same checks as Apply-GameExePatch.ps1: stock SHA-256 first, a backup
  (BaseMod.dll.orig), and the result hash checked.
  Supports -Check and -Undo. Close the game before you run it.
#>
[CmdletBinding()]
param(
  [string]$GameDir = '',
  [switch]$Check,
  [switch]$Undo
)
$ErrorActionPreference = 'Stop'
if (-not $GameDir) { $GameDir = Split-Path -Parent $PSScriptRoot }
& (Join-Path $PSScriptRoot 'Apply-GameExePatch.ps1') -GameDir $GameDir -PatchFile (Join-Path $PSScriptRoot 'basemod-patches.json') -Check:$Check -Undo:$Undo
exit $LASTEXITCODE
