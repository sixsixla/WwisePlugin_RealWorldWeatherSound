[CmdletBinding()]
param(
    [ValidateSet('Release')][string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

$productRoot = Get-ProductRoot
$artifactsRoot = Join-Path $productRoot 'Artifacts'
$manifestPath = Join-Path $artifactsRoot 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Staging manifest is missing: '$manifestPath'."
}

$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1) {
    throw "Unsupported staging manifest schemaVersion '$($manifest.schemaVersion)'."
}
$requiredIds = @(
    'authoring-dll',
    'authoring-xml',
    'runtime-static-library',
    'runtime-factory-header',
    'runtime-scene-api-header'
)
if ($manifest.entries.Count -ne $requiredIds.Count) {
    throw "The v1 staging manifest must contain exactly $($requiredIds.Count) entries."
}
foreach ($requiredId in $requiredIds) {
    if (@($manifest.entries | Where-Object { $_.id -eq $requiredId }).Count -ne 1) {
        throw "The v1 staging manifest must contain exactly one '$requiredId' entry."
    }
}

$resolvedEntries = @()
$seenStagePaths = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $manifest.entries) {
    if (-not $entry.source -or -not $entry.stage) {
        throw "Manifest entry '$($entry.id)' must declare source and stage paths."
    }
    if ([System.IO.Path]::IsPathRooted([string]$entry.source) -or [System.IO.Path]::IsPathRooted([string]$entry.stage)) {
        throw "Manifest entry '$($entry.id)' must use product-relative paths."
    }

    $sourceRelative = ([string]$entry.source).Replace('{configuration}', $Configuration)
    $stageRelative = ([string]$entry.stage).Replace('{configuration}', $Configuration)
    $source = Assert-PathUnderRoot -Path (Join-Path $productRoot $sourceRelative) -Root $productRoot -Description "Source for '$($entry.id)'"
    $stage = Assert-PathUnderRoot -Path (Join-Path $productRoot $stageRelative) -Root $artifactsRoot -Description "Stage path for '$($entry.id)'"
    if (-not $seenStagePaths.Add($stage)) {
        throw "Manifest entries resolve to the same stage path: '$stage'."
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Manifest source for '$($entry.id)' is missing: '$source'. Run Scripts\Build.ps1 first."
    }
    if ((Get-Item -LiteralPath $source).Length -eq 0) {
        throw "Manifest source for '$($entry.id)' is empty: '$source'."
    }

    $resolvedEntries += [pscustomobject]@{ Definition = $entry; Source = $source; Stage = $stage }
}

foreach ($stageDirectoryName in @('Authoring', 'Runtime')) {
    $stageDirectory = Assert-PathUnderRoot -Path (Join-Path $artifactsRoot $stageDirectoryName) -Root $artifactsRoot -Description 'Staging directory'
    if (Test-Path -LiteralPath $stageDirectory) {
        Remove-Item -LiteralPath $stageDirectory -Recurse -Force
    }
}

$recordEntries = @()
foreach ($resolved in $resolvedEntries) {
    $destinationDirectory = Split-Path -Parent $resolved.Stage
    [void](New-Item -ItemType Directory -Path $destinationDirectory -Force)
    Copy-Item -LiteralPath $resolved.Source -Destination $resolved.Stage -Force
    $hash = Get-FileHash -LiteralPath $resolved.Stage -Algorithm SHA256
    $recordEntries += [ordered]@{
        id = $resolved.Definition.id
        path = [string]$resolved.Definition.stage.Replace('{configuration}', $Configuration)
        sha256 = $hash.Hash.ToLowerInvariant()
        size = (Get-Item -LiteralPath $resolved.Stage).Length
    }
}

$expectedPaths = @($resolvedEntries | ForEach-Object { [System.IO.Path]::GetFullPath($_.Stage) })
$actualPaths = @(
    foreach ($stageDirectoryName in @('Authoring', 'Runtime')) {
        $directory = Join-Path $artifactsRoot $stageDirectoryName
        if (Test-Path -LiteralPath $directory) {
            Get-ChildItem -LiteralPath $directory -Recurse -File | ForEach-Object { $_.FullName }
        }
    }
)
$unexpected = @($actualPaths | Where-Object { $_ -notin $expectedPaths })
if ($unexpected.Count -gt 0) {
    throw "Unexpected files remain in the minimal staging directories:`r`n$($unexpected -join "`r`n")"
}

$record = [ordered]@{
    schemaVersion = 1
    product = $manifest.product
    wwiseVersion = $manifest.wwiseVersion
    configuration = $Configuration
    stagedAtUtc = [DateTime]::UtcNow.ToString('o')
    entries = $recordEntries
}
$recordPath = Join-Path $artifactsRoot 'stage-record.json'
$json = $record | ConvertTo-Json -Depth 6
[System.IO.File]::WriteAllText($recordPath, $json + [Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "Staged $($recordEntries.Count) minimal files under '$artifactsRoot'."
