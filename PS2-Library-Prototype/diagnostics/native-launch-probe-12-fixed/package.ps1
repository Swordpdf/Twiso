# SPDX-License-Identifier: GPL-3.0-or-later
# Creates a local app transport ZIP only. No FTP or console mutation.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskApp = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'dist/PPSA99202'))
$taskZip = Join-Path $PSScriptRoot 'PPSA99202-launch-probe-12.zip'
if (Test-Path -LiteralPath $taskZip) { throw 'Preserving existing Probe 12 archive.' }
$taskExpected = @('eboot.bin','assets/banner.txt','sce_module/libc.prx','sce_sys/icon0.png','sce_sys/pic0.png','sce_sys/pic1.png','sce_sys/param.json')
$taskActual = @(Get-ChildItem -LiteralPath $taskApp -Recurse -File | ForEach-Object {
    $_.FullName.Substring($taskApp.Length + 1).Replace('\','/')
})
if (Compare-Object ($taskExpected | Sort-Object) ($taskActual | Sort-Object)) { throw 'Unexpected app-folder contents.' }
$taskParam = Get-Content -LiteralPath (Join-Path $taskApp 'sce_sys/param.json') -Raw | ConvertFrom-Json
if ($taskParam.titleId -cne 'PPSA99202' -or $taskParam.contentVersion -cne '01.000.001') { throw 'Unexpected app identity.' }
[System.IO.Compression.ZipFile]::CreateFromDirectory($taskApp, $taskZip, [System.IO.Compression.CompressionLevel]::Optimal, $true)
Get-Item -LiteralPath $taskZip | Select-Object FullName,Length
Get-FileHash -LiteralPath $taskZip -Algorithm SHA256 | Select-Object Hash


