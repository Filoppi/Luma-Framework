<#
.SYNOPSIS
    Ensures a game project's vcpkg manifest + overlay configuration carry the DXP
    dependency, merging into existing files (never overwriting or deleting user
    content). Called by the LumaEnsureDxpManifest target when UseLumaDXP=true.
#>
param(
    [Parameter(Mandatory = $true)][string]$ProjectDir,
    [Parameter(Mandatory = $true)][string]$ManifestName
)

$ErrorActionPreference = "Stop"

$manifestPath = Join-Path $ProjectDir "vcpkg.json"
if (Test-Path $manifestPath) {
    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
} else {
    $manifest = [pscustomobject]@{
        "version-string" = "1.0.0"
        description      = "Luma mod manifest (auto-ensured by Luma.DXP.props)"
    }
}

# Defaults for a freshly created manifest
if (-not $manifest.name) { $manifest | Add-Member -MemberType NoteProperty -Name "name" -Value $ManifestName -Force }
if (-not $manifest."version-string") { $manifest | Add-Member -MemberType NoteProperty -Name "version-string" -Value "1.0.0" -Force }
if (-not $manifest.description) { $manifest | Add-Member -MemberType NoteProperty -Name "description" -Value "Luma mod manifest (auto-ensured by Luma.DXP.props)" -Force }

# Ensure the dxp dependency exists (handles both "dxp" and { "name": "dxp" } forms).
# Normalize string deps to objects so ConvertTo-Json (PS 5.1) serializes uniformly.
$deps = if ($manifest.dependencies) { @($manifest.dependencies) } else { @() }
$deps = @($deps | ForEach-Object { if ($_ -is [string]) { [pscustomobject]@{ name = $_ } } else { $_ } })
$hasDxp = $deps | Where-Object { $_ -is [string] -and $_ -eq "dxp" } | Select-Object -First 1
if (-not $hasDxp) {
    $hasDxp = $deps | Where-Object { $_ -is [psobject] -and $_.name -eq "dxp" } | Select-Object -First 1
}
if (-not $hasDxp) {
    $deps += [pscustomobject]@{ name = "dxp" }
    $manifest | Add-Member -MemberType NoteProperty -Name "dependencies" -Value $deps -Force
}
$manifestJson = $manifest | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($manifestPath, $manifestJson, [System.Text.UTF8Encoding]::new($false))

# Overlay configuration: ensure the dxp overlay port is on the search path
$configPath = Join-Path $ProjectDir "vcpkg-configuration.json"
if (Test-Path $configPath) {
    $config = Get-Content $configPath -Raw | ConvertFrom-Json
} else {
    $config = [pscustomobject]@{}
}
$ports = @(if ($config."overlay-ports") { $config."overlay-ports" })
$hasOverlay = $ports | Where-Object { $_ -eq "../../External/vcpkg-ports" } | Select-Object -First 1
if (-not $hasOverlay) {
    $ports += "../../External/vcpkg-ports"
    $config | Add-Member -MemberType NoteProperty -Name "overlay-ports" -Value $ports -Force
}
$configJson = $config | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($configPath, $configJson, [System.Text.UTF8Encoding]::new($false))

Write-Host "Luma: ensured DXP vcpkg configuration in $ProjectDir (dxp dependency + overlay port)"
