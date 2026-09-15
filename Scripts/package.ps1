<#
.SYNOPSIS
    Package a Luma game addon locally, replicating the CI packaging
    (.github/workflows/build_and_release.yml "Create ZIP per addon").

.DESCRIPTION
    Produces the same layout the CI ships: a zip containing the addon at the
    root plus a "Luma/" folder (the shaders mount, filtered to this project),
    with the project-level (Core/Textures) folders only included when the
    project opts in via UseLumaFastNoise, plus the runtime DLLs the project
    needs (dxcompiler.dll for DXP, d3dcompiler_47.dll, ReShade as dxgi.dll,
    NGX DLSS when opted in).

.EXAMPLE
    .\scripts\package.ps1 -Project "Final Fantasy XV" -Config "Development-Release" -Platform "x64"
    .\scripts\package.ps1 -Project "Metro Redux" -Config "Publishing-Release" -Platform "x64"
#>
param(
    [string]$Project,
    [ValidateSet("Development-Debug", "Development-Release", "Test-Release", "Publishing-Release")]
    [string]$Config = "Development-Release",
    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",
    [string]$OutDir = "",
    [string]$AddonPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent

function Test-PropEnabled([string]$vcxprojPath, [string]$propName) {
    $content = Get-Content $vcxprojPath -Raw
    return $content -match "<$propName>\s*true\s*</$propName>"
}

function Find-ProjectDir {
    param([string]$RepoRoot, [string]$ProjectName)
    foreach ($rel in @("Source\Games\$ProjectName", "Source\Games\_$ProjectName", "Source\$ProjectName")) {
        $dir = Join-Path $RepoRoot $rel
        if (Test-Path $dir -PathType Container) { return $dir }
    }
    return $null
}

function Copy-PackageShaders {
    param([string]$Source, [string]$Destination, [string]$Mount, [string]$RelativePath = "")

    # Enumerate one level at a time so excluded directories are never entered.
    foreach ($entry in Get-ChildItem -LiteralPath $Source) {
        $relative = if ($RelativePath) { Join-Path $RelativePath $entry.Name } else { $entry.Name }
        $destinationPath = Join-Path $Destination $entry.Name
        if ($entry.PSIsContainer) {
            if ($Mount -eq $Project -and $entry.Name -like "Dump*") { continue }
            if ($entry.Name -in @("Unused", "Dev", "Sample")) { continue }
            if (-not $RelativePath -and $Mount -eq "Global" -and $entry.Name -eq "Textures" -and -not $useLumaFastNoise) { continue }
            Copy-PackageShaders -Source $entry.FullName -Destination $destinationPath -Mount $Mount -RelativePath $relative
            continue
        }

        # Exclude compiled dumps even when they sit inside a texture directory.
        if ($entry.Extension -ieq ".cso") { continue }
        # ".h" too, as some shaders include headers (e.g. Deus Ex Mankind Divided's "shared.h")
        $isShader = $entry.Extension -in @(".hlsl", ".hlsli", ".h")
        $isRecipe = $entry.Name.EndsWith('.recipe.yml', [System.StringComparison]::OrdinalIgnoreCase)
        $isTexture = $relative -match '(^|[\\/])Textures[\\/]'
        if (-not ($isShader -or $isRecipe -or $isTexture)) { continue }

        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
        Copy-Item -LiteralPath $entry.FullName -Destination $destinationPath -Force
    }
}

$projectDir = Find-ProjectDir -RepoRoot $repoRoot -ProjectName $Project
if (-not $projectDir) {
    Write-Error "Project folder not found (or no vcxproj): Source\Games\$Project"
    exit 1
}
# The vcxproj filename doesn't always match the display name (e.g. _Template, _Generic Mod)
$vcxproj = Get-ChildItem -Path $projectDir -Filter "*.vcxproj" -File -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $vcxproj) {
    Write-Error "Project folder not found (or no vcxproj): $projectDir"
    exit 1
}
$vcxproj = $vcxproj.FullName

# Feature opt-ins, mirroring the CI scan
$useLumaFastNoise = Test-PropEnabled $vcxproj "UseLumaFastNoise"
$useLumaDXP = Test-PropEnabled $vcxproj "UseLumaDXP"
$useLumaNGX = Test-PropEnabled $vcxproj "UseLumaNGX"
Write-Host "Opt-ins: UseLumaFastNoise=$useLumaFastNoise UseLumaDXP=$useLumaDXP UseLumaNGX=$useLumaNGX"

# Locate the addon. When packaging runs from the LumaPackage build target the
# exact build output is passed in (-AddonPath = $(TargetPath)); standalone runs
# scan this project's build output and the repo-root solution output instead.
$addonFile = $null
if (-not [string]::IsNullOrEmpty($AddonPath) -and (Test-Path $AddonPath)) {
    $addonFile = Get-Item $AddonPath
} else {
    $addonCandidates = @()
    $projectAddonDir = Join-Path $projectDir "Binaries\$Platform-$Config"
    foreach ($dir in @($projectAddonDir, (Join-Path $repoRoot "Binaries\$Platform-$Config"))) {
        if (Test-Path $dir) {
            $addonCandidates += Get-ChildItem -Path $dir -Filter "*.addon" -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match [regex]::Escape($Project) }
        }
    }
    $addonFile = $addonCandidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
}
if (-not $addonFile) {
    Write-Error "Addon not found for '$Project' (build the project first)"
    exit 1
}
Write-Host "Using addon: $($addonFile.FullName) ($($addonFile.LastWriteTime))"

# Zip name, mirroring the CI
$zipName = "Luma-$Project"
if ($Config -eq "Test-Release") { $zipName += "-Test" }
if ($Config -eq "Development-Release") { $zipName += "-Dev" }
if ($Config -eq "Development-Debug") { $zipName += "-Dev-Dbg" }
if ($Platform -eq "Win32") { $zipName += "-x32" }
$zipName = $zipName -replace ' ', '_'
$zipName += ".zip"

# Temp staging dir (unique per run, in the system temp so interrupted builds
# don't pollute the repo tree)
$tempDir = Join-Path $env:TEMP ("Luma-Package-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Path "$tempDir\Luma" -Force | Out-Null

    # Copy only the three package mounts, filtering before copying any files.
    # Never enumerate other games or descend into this game's Dump directories.
    foreach ($mount in @("Global", "Includes", $Project) | Select-Object -Unique) {
        $source = Join-Path $repoRoot "Shaders\$mount"
        if (Test-Path -LiteralPath $source -PathType Container) {
            Write-Host "Staging shaders: $mount"
            Copy-PackageShaders -Source $source -Destination (Join-Path "$tempDir\Luma" $mount) -Mount $mount
        }
    }

    # Runtime DLLs (dxcompiler.dll is intentionally not shipped: Luma only uses the
    #    SM5 recipe path, which is native — DXC/DXIL is only needed once SM6 is used)
    $d3dCompilerSrc = if ($Platform -eq "Win32") { Join-Path $repoRoot "Shaders\Decompiler\d3dcompiler_47_x32.dll" }
                      else { Join-Path $repoRoot "Shaders\Decompiler\d3dcompiler_47.dll" }
    if (Test-Path $d3dCompilerSrc) { Copy-Item $d3dCompilerSrc -Destination "$tempDir\Luma" -Force }
    $reshadeSrc = if ($Platform -eq "Win32") { Join-Path $repoRoot "Source\External\reshade\bin\Win32\Release\ReShade32.dll" }
                  else { Join-Path $repoRoot "Source\External\reshade\bin\x64\Release\ReShade64.dll" }
    if (Test-Path $reshadeSrc) { Copy-Item $reshadeSrc -Destination (Join-Path $tempDir "dxgi.dll") -Force }
    if ($useLumaNGX) {
        $ngxSrc = Join-Path $repoRoot "Source\External\NGX\bin\dev\nvngx_dlss.dll"
        if ($Config -notlike "Development*") { $ngxSrc = Join-Path $repoRoot "Source\External\NGX\bin\rel\nvngx_dlss.dll" }
        if (Test-Path $ngxSrc) { Copy-Item $ngxSrc -Destination $tempDir -Force }
    }

    # Addon at the zip root, under the canonical Luma-<Project>.addon name
    Copy-Item $addonFile.FullName -Destination (Join-Path $tempDir "Luma-$Project.addon") -Force

    # Zip — next to the addon by default
    if ([string]::IsNullOrEmpty($OutDir)) { $OutDir = $addonFile.DirectoryName }
    $OutDir = $OutDir.TrimEnd('\')
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    $zipPath = Join-Path $OutDir $zipName
    Compress-Archive -Path "$tempDir\*" -DestinationPath $zipPath -Force
    Write-Host "Packaged: $zipPath"
} finally {
    # Only remove the unique staging directory created by this invocation.
    $resolvedTempDir = [IO.Path]::GetFullPath($tempDir)
    $tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($resolvedTempDir.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path $resolvedTempDir -Leaf) -match '^Luma-Package-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolvedTempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
