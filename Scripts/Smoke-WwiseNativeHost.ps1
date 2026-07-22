[CmdletBinding()]
param(
    [string]$WwiseRoot = 'E:\WwiseSoft2023\Wwise_2023.1.19.8928',
    [string]$FixtureRoot,
    [string]$Bank = 'RWWA_Effect_Baseline.bnk',
    [string]$Event = 'RWWA_Smoke_Effect_Bank_Event',
    [string]$PluginDir,
    [ValidateRange(1, 3600000)][int]$DurationMs = 1000,
    [string]$Report,
    [string]$SceneJson,
    [ValidateSet('changed', 'wet-bypass', 'geometry-disabled', 'transparent')][string]$Expectation = 'changed',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

function Write-SmokeFailureReport {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Stage,
        [Parameter(Mandatory = $true)][string]$Message,
        [Parameter(Mandatory = $true)][string[]]$MissingPaths
    )

    $parent = Split-Path -Parent $Path
    if ($parent) {
        [void](New-Item -ItemType Directory -Path $parent -Force)
    }
    [ordered]@{
        schemaVersion = 1
        success       = $false
        stage         = $Stage
        message       = $Message
        missingPaths  = $MissingPaths
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Path -Encoding utf8
}

$productRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nativeHostRoot = Join-Path $productRoot 'Build\NativeHost'
$executablePath = Join-Path $nativeHostRoot 'bin\Release\rwwa_native_host.exe'
if ([string]::IsNullOrWhiteSpace($FixtureRoot)) {
    $FixtureRoot = Join-Path $nativeHostRoot 'Fixture'
}
if ([string]::IsNullOrWhiteSpace($PluginDir)) {
    $PluginDir = Join-Path $productRoot 'Build\Wwise\Runtime\x64_vc170\Release\bin'
}
if ([string]::IsNullOrWhiteSpace($Report)) {
    $runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $Report = Join-Path $nativeHostRoot "native-host-smoke-$runId.json"
}

$FixtureRoot = [System.IO.Path]::GetFullPath($FixtureRoot)
$PluginDir = [System.IO.Path]::GetFullPath($PluginDir)
$Report = [System.IO.Path]::GetFullPath($Report)
if (-not [string]::IsNullOrWhiteSpace($SceneJson)) {
    $SceneJson = [System.IO.Path]::GetFullPath($SceneJson)
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'Build-NativeHost.ps1') -WwiseRoot $WwiseRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Native Host build script failed with exit code $LASTEXITCODE."
    }
}

$missingPaths = @()
if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    $missingPaths += $executablePath
}
if (-not (Test-Path -LiteralPath $FixtureRoot -PathType Container)) {
    $missingPaths += $FixtureRoot
}
if (-not (Test-Path -LiteralPath (Join-Path $PluginDir 'RealWorldWeatherAcoustics.dll') -PathType Leaf)) {
    $missingPaths += Join-Path $PluginDir 'RealWorldWeatherAcoustics.dll'
}
if ($SceneJson -and -not (Test-Path -LiteralPath $SceneJson -PathType Leaf)) {
    $missingPaths += $SceneJson
}
if ($missingPaths.Count -gt 0) {
    $message = 'Native Host smoke prerequisites are missing. Generate the retained Authoring fixture before running the native smoke.'
    Write-SmokeFailureReport -Path $Report -Stage 'preflight' -Message $message -MissingPaths $missingPaths
    [Console]::Error.WriteLine("$message Report: '$Report'. Missing: $($missingPaths -join ', ')")
    exit 2
}

$bankPath = Join-Path $FixtureRoot $Bank
if (-not (Test-Path -LiteralPath $bankPath -PathType Leaf)) {
    $candidate = Get-ChildItem -LiteralPath $FixtureRoot -Recurse -File -Filter $Bank |
        Sort-Object -Property LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($candidate) {
        $FixtureRoot = $candidate.DirectoryName
        $bankPath = $candidate.FullName
    }
    elseif (-not (Test-Path -LiteralPath (Join-Path $FixtureRoot 'Init.bnk') -PathType Leaf)) {
        $latestInitBank = Get-ChildItem -LiteralPath $FixtureRoot -Recurse -File -Filter 'Init.bnk' |
            Sort-Object -Property LastWriteTimeUtc -Descending |
            Select-Object -First 1
        if ($latestInitBank) {
            $FixtureRoot = $latestInitBank.DirectoryName
            $bankPath = Join-Path $FixtureRoot $Bank
        }
    }
}

$missingPaths = @()
if (-not (Test-Path -LiteralPath $bankPath -PathType Leaf)) {
    $missingPaths += $bankPath
}
$initBankPath = Join-Path $FixtureRoot 'Init.bnk'
if (-not $Bank.Equals('Init.bnk', [System.StringComparison]::OrdinalIgnoreCase) -and
    -not (Test-Path -LiteralPath $initBankPath -PathType Leaf)) {
    $missingPaths += $initBankPath
}
if ($missingPaths.Count -gt 0) {
    $message = 'Native Host SoundBank fixture is incomplete; the smoke was not run.'
    Write-SmokeFailureReport -Path $Report -Stage 'fixture-preflight' -Message $message -MissingPaths $missingPaths
    [Console]::Error.WriteLine("$message Report: '$Report'. Missing: $($missingPaths -join ', ')")
    exit 3
}

$nativeArguments = @(
    '--bank-dir', $FixtureRoot,
    '--bank', $Bank,
    '--event', $Event,
    '--plugin-dir', $PluginDir,
    '--duration-ms', [string]$DurationMs,
    '--report', $Report,
    '--expect-difference', $Expectation
)
if ($SceneJson) {
    $nativeArguments += @('--scene-json', $SceneJson)
}

& $executablePath @nativeArguments
$nativeExitCode = $LASTEXITCODE

if (-not (Test-Path -LiteralPath $Report -PathType Leaf)) {
    throw "Native Host did not write its report: '$Report'."
}

$nativeReport = Get-Content -LiteralPath $Report -Raw | ConvertFrom-Json
if ($nativeExitCode -ne 0 -or -not $nativeReport.exitStatus.success) {
    throw "Native Host smoke failed with exit code $nativeExitCode at stage '$($nativeReport.exitStatus.stage)'. Report: '$Report'."
}

Write-Output "Native Host smoke passed. Report: '$Report'."
