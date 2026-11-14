# Verifies that a portable FurnitureOS folder is self-contained: every DLL imported by
# any shipped .exe/.dll is either in the folder (or its plugin subfolders) or a Windows
# system DLL. Requires dumpbin (Visual Studio developer environment).
# SPDX-License-Identifier: AGPL-3.0-only
param([Parameter(Mandatory = $true)][string]$Folder)
$ErrorActionPreference = 'Stop'
$shipped = @{}
Get-ChildItem -Path $Folder -Recurse -Include *.dll, *.exe | ForEach-Object { $shipped[$_.Name.ToLower()] = $true }
$system = "$env:WINDIR\System32"
$missing = @()
Get-ChildItem -Path $Folder -Recurse -Include *.dll, *.exe | ForEach-Object {
    $file = $_.FullName
    $deps = & dumpbin /nologo /dependents $file | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim().ToLower() }
    foreach ($d in $deps) {
        if ($shipped.ContainsKey($d)) { continue }
        if ($d -like 'api-ms-win-*' -or $d -like 'ext-ms-*') { continue }   # API sets resolved by the OS loader
        if (Test-Path (Join-Path $system $d)) { continue }
        $missing += "$($_.Name) -> $d"
    }
}
foreach ($exe in 'furnitureos.exe', 'fos.exe') {
    if (-not (Test-Path (Join-Path $Folder $exe))) { $missing += "missing executable $exe" }
}
if (-not (Test-Path (Join-Path $Folder 'platforms\qwindows.dll'))) { $missing += 'missing Qt platform plugin platforms\qwindows.dll' }
if ($missing.Count -gt 0) {
    Write-Host "Deployment is NOT self-contained:"; $missing | ForEach-Object { Write-Host "  $_" }; exit 1
}
Write-Host "Deployment is self-contained ($($shipped.Count) binaries checked)."
