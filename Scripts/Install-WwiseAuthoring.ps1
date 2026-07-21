[CmdletBinding()]
param(
    [string]$WwiseRoot,
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

$environment = Resolve-WeatherEnvironment -WwiseRoot $WwiseRoot
$manifestPath = Join-Path $environment.ArtifactsRoot 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Installation manifest is missing: '$manifestPath'."
}
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or $manifest.wwiseVersion -ne $environment.WwiseVersion) {
    throw "The installation manifest does not match Wwise $($environment.WwiseVersion) schema v1."
}

$stageRecordPath = Join-Path $environment.ArtifactsRoot 'stage-record.json'
if (-not (Test-Path -LiteralPath $stageRecordPath -PathType Leaf)) {
    throw "Staging record is missing: '$stageRecordPath'. Run Scripts\Stage.ps1 first."
}
$stageRecord = Get-Content -Raw -LiteralPath $stageRecordPath | ConvertFrom-Json
if ($stageRecord.schemaVersion -ne 1 -or $stageRecord.wwiseVersion -ne $environment.WwiseVersion -or $stageRecord.configuration -ne $Configuration) {
    throw 'The staging record does not match the requested Wwise version/configuration.'
}

$installEntries = @($manifest.entries | Where-Object { $_.install })
if ($installEntries.Count -ne 2) {
    throw "Authoring installation must contain exactly two manifest entries (DLL and XML), but found $($installEntries.Count)."
}
$extensions = @($installEntries | ForEach-Object { [System.IO.Path]::GetExtension([string]$_.install).ToLowerInvariant() } | Sort-Object -Unique)
if ($extensions.Count -ne 2 -or '.dll' -notin $extensions -or '.xml' -notin $extensions) {
    throw 'Authoring installation manifest must contain exactly one DLL and one XML file.'
}
$requiredFileNames = @('RealWorldWeatherAcoustics.dll', 'RealWorldWeatherAcoustics.xml')
$manifestFileNames = @($installEntries | ForEach-Object { [System.IO.Path]::GetFileName([string]$_.install) } | Sort-Object)
if (($manifestFileNames -join '|') -ne (($requiredFileNames | Sort-Object) -join '|')) {
    throw "Authoring installation is restricted to: $($requiredFileNames -join ', ')."
}

$operations = @()
foreach ($entry in $installEntries) {
    if ([System.IO.Path]::IsPathRooted([string]$entry.stage) -or [System.IO.Path]::IsPathRooted([string]$entry.install)) {
        throw "Manifest entry '$($entry.id)' must use relative stage and install paths."
    }
    $sourceRelative = ([string]$entry.stage).Replace('{configuration}', $Configuration)
    $installRelative = ([string]$entry.install).Replace('{configuration}', $Configuration)
    $source = Assert-PathUnderRoot -Path (Join-Path $environment.ProductRoot $sourceRelative) -Root $environment.ArtifactsRoot -Description "Staged source for '$($entry.id)'"
    $destination = Assert-PathUnderRoot -Path (Join-Path $environment.WwiseRoot $installRelative) -Root $environment.WwiseRoot -Description "Wwise destination for '$($entry.id)'"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Staged install source is missing: '$source'. Run Scripts\Stage.ps1 first."
    }
    $recordEntry = @($stageRecord.entries | Where-Object { $_.id -eq $entry.id })
    if ($recordEntry.Count -ne 1) {
        throw "Staging record must contain exactly one '$($entry.id)' entry."
    }
    if ([string]$recordEntry[0].path -ne $sourceRelative) {
        throw "Staging record path for '$($entry.id)' does not match the installation manifest."
    }
    $actualHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne [string]$recordEntry[0].sha256) {
        throw "Staged file hash no longer matches stage-record.json: '$source'. Re-run Scripts\Stage.ps1."
    }
    $operations += [pscustomobject]@{ Id = $entry.id; Source = $source; Destination = $destination }
}

if (-not $Apply) {
    Write-Host 'DRY-RUN: no files will be changed. Planned Authoring installation:'
    foreach ($operation in $operations) {
        Write-Host "  $($operation.Source) -> $($operation.Destination)"
    }
    Write-Host 'Re-run with -Apply to verify Wwise is stopped, back up existing files, and install.'
    return
}

if (Get-Process -Name 'Wwise' -ErrorAction SilentlyContinue) {
    throw 'Wwise Authoring is running. Close Wwise before using -Apply.'
}

$destinationRoot = Join-Path $environment.WwiseRoot 'Authoring\x64\Release\bin\Plugins'
if (-not (Test-Path -LiteralPath $destinationRoot -PathType Container)) {
    throw "Expected Wwise Authoring plug-in directory is missing: '$destinationRoot'."
}
foreach ($operation in $operations) {
    $actualDestinationRoot = Split-Path -Parent $operation.Destination
    if (-not $actualDestinationRoot.Equals($destinationRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to install '$($operation.Id)' outside the exact Authoring plug-in directory: '$actualDestinationRoot'."
    }
}

$existing = @($operations | Where-Object { Test-Path -LiteralPath $_.Destination -PathType Leaf })
if ($existing.Count -gt 0) {
    $backupRoot = Assert-PathUnderRoot `
        -Path (Join-Path $environment.ArtifactsRoot ('InstallBackup\' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))) `
        -Root $environment.ArtifactsRoot `
        -Description 'Install backup directory'
    [void](New-Item -ItemType Directory -Path $backupRoot -Force)
    foreach ($operation in $existing) {
        Copy-Item -LiteralPath $operation.Destination -Destination (Join-Path $backupRoot ([System.IO.Path]::GetFileName($operation.Destination)))
    }
    Write-Host "Backed up $($existing.Count) existing file(s) to '$backupRoot'."
}

foreach ($operation in $operations) {
    Copy-Item -LiteralPath $operation.Source -Destination $operation.Destination -Force
    $sourceHash = (Get-FileHash -LiteralPath $operation.Source -Algorithm SHA256).Hash
    $destinationHash = (Get-FileHash -LiteralPath $operation.Destination -Algorithm SHA256).Hash
    if ($sourceHash -ne $destinationHash) {
        throw "Hash verification failed after installing '$($operation.Id)'."
    }
    Write-Host "Installed '$($operation.Destination)'."
}

Write-Host 'Wwise Authoring installation complete.'
