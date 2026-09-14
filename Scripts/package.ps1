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
New-Item -ItemType Directory -Path "$tempDir\Luma" -Force | Out-Null

# 1. Copy the shaders mount (which now also carries the textures)
Copy-Item -Path (Join-Path $repoRoot "Shaders\*") -Destination "$tempDir\Luma" -Recurse -Force

# 2. Keep only this project's folders (Global/Includes/<Project> + Core for opt-ins)
$allowedDirs = @("Global", "Includes", $Project)
Get-ChildItem -Path "$tempDir\Luma" -Directory | ForEach-Object {
    if ($allowedDirs -notcontains $_.Name) {
        Write-Host "Removing disallowed folder: $($_.Name)"
        Remove-Item $_.FullName -Recurse -Force
    } elseif (-not $Config.StartsWith("Development")) {
        foreach ($folder in @("Unused", "Dev", "Sample")) {
            $target = Join-Path $_.FullName $folder
            if (Test-Path $target) { Remove-Item $target -Recurse -Force }
        }
    }
}

# 2b. Project-wide (Global) textures only ship to opt-in projects
if (-not $useLumaFastNoise) {
    $globalTexturesDir = Join-Path "$tempDir\Luma" "Global\Textures"
    if (Test-Path $globalTexturesDir) {
        Write-Host "Removing project-wide textures for non-opt-in project: $globalTexturesDir"
        Remove-Item $globalTexturesDir -Recurse -Force
    }
}

# 3. Keep shader/recipe/texture files only
$allowedExtensions = @(".hlsl", ".hlsli")
Get-ChildItem -Path "$tempDir\Luma" -Recurse -File | ForEach-Object {
    # Keep Windows PowerShell 5.1 compatible - it lacks [IO.Path]::GetRelativePath, so compute it manually
    $relativePath = $_.FullName.Substring("$tempDir\Luma\".Length)
    $isRecipeFile = $_.Name.EndsWith('.recipe.yml', [System.StringComparison]::OrdinalIgnoreCase)
    $isTextureFile = $relativePath -match 'Textures[\\/]'
    if (($allowedExtensions -notcontains $_.Extension.ToLower()) -and (-not $isRecipeFile) -and (-not $isTextureFile)) {
        Remove-Item $_.FullName -Force
    }
}

# 4. Runtime DLLs (dxcompiler.dll is intentionally not shipped: Luma only uses the
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

# 5. Addon at the zip root, under the canonical Luma-<Project>.addon name
Copy-Item $addonFile.FullName -Destination (Join-Path $tempDir "Luma-$Project.addon") -Force

# 6. Zip — next to the addon by default
if ([string]::IsNullOrEmpty($OutDir)) { $OutDir = $addonFile.DirectoryName }
$OutDir = $OutDir.TrimEnd('\')
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$zipPath = Join-Path $OutDir $zipName
try {
    Compress-Archive -Path "$tempDir\*" -DestinationPath $zipPath -Force
    Write-Host "Packaged: $zipPath"
} finally {
    Remove-Item -Recurse -Force $tempDir -ErrorAction SilentlyContinue
}
