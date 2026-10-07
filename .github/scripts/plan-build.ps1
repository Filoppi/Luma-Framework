# Decides which projects each build cell compiles, and splits the big x64 cells over a few runners.
# On pull requests we only build the games whose project folder or "Shaders/<Game>/" the PR touches. Any other change
# (Core, props, the solution, shared shaders, scripts, CI) builds everything, like pushes to main do. Markdown files are ignored.
# Writes "matrix" (the build job's "strategy.matrix") and "count" (its number of cells) to "$GITHUB_OUTPUT".
# Each cell lists its project paths, which the build job turns into a solution filter. A "/t:" list would stop at the first
# project that fails.
param(
    [string]$EventName = $env:GITHUB_EVENT_NAME,
    # On pull_request, actions/checkout checks out GitHub's merge commit, so HEAD^1 is the base branch
    [string]$DiffBase = "HEAD^1",
    [string]$DiffHead = "HEAD"
)
$ErrorActionPreference = "Stop"
$x64Shards = 3

$sln = Get-Content "Luma.sln" -Raw
$solutionFolderType = "2150E333-8FDC-42A3-9474-1A3956D46DE8"
$projects = @{}
foreach ($m in [regex]::Matches($sln, 'Project\("\{([^}]+)\}"\) = "([^"]+)", "([^"]+)", "\{([^}]+)\}"')) {
    $type, $name, $path, $guid = $m.Groups[1].Value, $m.Groups[2].Value, $m.Groups[3].Value, $m.Groups[4].Value
    if ($type -eq $solutionFolderType) { continue }
    $dir = $path -replace '\\', '/' # the solution uses Windows separators, and this also runs on Linux
    $projects[$guid] = [pscustomobject]@{ Name = $name; Path = $path; Dir = $dir.Substring(0, $dir.LastIndexOf('/')); Cells = @() }
}
foreach ($m in [regex]::Matches($sln, '\{([0-9A-Fa-f-]+)\}\.([^|\r\n]+\|[^.\r\n]+)\.Build\.0 = ([^\r\n]+)')) {
    $guid, $cell, $mapped = $m.Groups[1].Value, $m.Groups[2].Value, $m.Groups[3].Value.Trim()
    if ($projects.ContainsKey($guid) -and $mapped -eq $cell) { $projects[$guid].Cells += $cell }
}

$selected = $null # $null means all of them
if ($EventName -eq "pull_request") {
    # With "--no-renames" a moved file lists both its old and new path, so the game it left gets built too
    $changed = @(git diff --no-renames --name-only $DiffBase $DiffHead)
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Couldn't diff against $DiffBase, building everything"
    }
    else {
        $selected = @{}
        foreach ($file in $changed) {
            if ($file -like "*.md") { continue }
            $owner = $projects.Values | Where-Object {
                $_.Name -ne "Core" -and ($file.StartsWith("$($_.Dir)/") -or $file.StartsWith("Shaders/$($_.Name)/"))
            } | Select-Object -First 1
            if (-not $owner) {
                Write-Host "`"$file`" isn't owned by a single game, building everything"
                $selected = $null
                break
            }
            $selected[$owner.Name] = $true
        }
    }
}

$include = @()
foreach ($config in "Development-Debug", "Development-Release", "Test-Release", "Publishing-Release") {
    foreach ($platform in "x64", "Win32") {
        $list = @($projects.Values | Where-Object { $_.Cells -contains "$config|$platform" -and ($null -eq $selected -or $selected[$_.Name]) } | Sort-Object Name)
        if ($list.Count -eq 0) { continue }
        $shards = if ($platform -eq "x64") { [Math]::Min($x64Shards, $list.Count) } else { 1 }
        for ($shard = 0; $shard -lt $shards; $shard++) {
            $part = @(for ($i = $shard; $i -lt $list.Count; $i += $shards) { $list[$i] })
            $include += [ordered]@{
                config   = $config
                platform = $platform
                shard    = "$($shard + 1)"
                projects = ($part.Name -join ';')
                paths    = ($part.Path -join ';')
            }
        }
    }
}

$scope = if ($null -eq $selected) { "everything" } else { "only $($selected.Keys -join ', ')" }
Write-Host "Building $scope in $($include.Count) cells"
foreach ($cell in $include) { Write-Host "  $($cell.config) $($cell.platform) #$($cell.shard): $($cell.projects)" }
$matrix = ConvertTo-Json -Compress -Depth 3 @{ include = $include }
if ($env:GITHUB_OUTPUT) {
    "matrix=$matrix" >> $env:GITHUB_OUTPUT
    "count=$($include.Count)" >> $env:GITHUB_OUTPUT
}
