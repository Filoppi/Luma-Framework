<#
.SYNOPSIS
    Ensures a game project's vcpkg manifest + overlay configuration carry the DXP
    dependency, merging into existing files (never overwriting or deleting user
    content). Called by the LumaEnsureDxpManifest target when UseLumaDXP=true.
    Also patches the vcxproj to remove any hardcoded VcpkgEnabled=false that would
    override Luma.Features.props' conditional VcpkgEnabled=true.
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
$ports = if ($config."overlay-ports") { @($config."overlay-ports") } else { @() }
$hasOverlay = $ports | Where-Object { $_ -eq "../../External/vcpkg-ports" } | Select-Object -First 1
if (-not $hasOverlay) {
    $ports += "../../External/vcpkg-ports"
    $config | Add-Member -MemberType NoteProperty -Name "overlay-ports" -Value $ports -Force
}
$configJson = $config | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($configPath, $configJson, [System.Text.UTF8Encoding]::new($false))

# Patch the vcxproj: remove hardcoded VcpkgEnabled=false / VcpkgEnableManifest=false
# that would override Luma.Features.props' conditional VcpkgEnabled=true.
# These are typically left by the _Template and block MSBuild's vcpkg integration.
$vcxprojPath = Join-Path $ProjectDir "${ManifestName}.vcxproj"
if (-not (Test-Path $vcxprojPath)) {
    # The vcxproj name may differ from the manifest name; search for any .vcxproj
    $vcxprojFiles = Get-ChildItem $ProjectDir -Filter "*.vcxproj"
    if ($vcxprojFiles.Count -eq 1) {
        $vcxprojPath = $vcxprojFiles[0].FullName
    } elseif ($vcxprojFiles.Count -gt 1) {
        Write-Warning "Multiple .vcxproj files found in $ProjectDir; skipping vcxproj patch."
        $vcxprojPath = $null
    }
}

if ($vcxprojPath -and (Test-Path $vcxprojPath)) {
    $vcxprojContent = [System.IO.File]::ReadAllText($vcxprojPath)
    $vcxprojModified = $false

    # Match the unconditional PropertyGroup that sets both VcpkgEnabled and
    # VcpkgEnableManifest to false. These block Luma.Features.props from
    # enabling vcpkg when UseLumaDXP=true.
    # Handles variable whitespace/indentation.
    $pattern = '(?s)\s*<PropertyGroup\s+Label="Vcpkg">\s*<VcpkgEnableManifest>false</VcpkgEnableManifest>\s*<VcpkgEnabled>false</VcpkgEnabled>\s*</PropertyGroup>\s*'
    if ($vcxprojContent -match $pattern) {
        $vcxprojContent = $vcxprojContent -replace $pattern, ""
        $vcxprojModified = $true
    }

    if ($vcxprojModified) {
        [System.IO.File]::WriteAllText($vcxprojPath, $vcxprojContent, [System.Text.UTF8Encoding]::new($false))
        Write-Host "Luma: removed hardcoded VcpkgEnabled=false from $vcxprojPath (Luma.Features.props now controls it)"
    }
}

Write-Host "Luma: ensured DXP vcpkg configuration in $ProjectDir (dxp dependency + overlay port)"
