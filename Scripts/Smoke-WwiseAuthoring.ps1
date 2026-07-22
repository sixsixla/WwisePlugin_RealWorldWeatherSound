[CmdletBinding()]
param(
    [string]$WwiseRoot,
    [string]$PythonWithWaapi,
    [string]$WaapiUrl = 'ws://127.0.0.1:8080/waapi',
    [ValidateRange(10, 300)][int]$StartupTimeoutSeconds = 90,
    [ValidateRange(0.5, 10.0)][double]$PlaybackSeconds = 1.5,
    [ValidateRange(-200.0, -1.0)][double]$SilenceFloorDb = -90.0,
    [ValidateRange(0, 4294967295)][long]$EffectClassId = 2031748099,
    [string]$EffectInputWav,
    [bool]$RequireRetainedRainDemo = $true,
    [switch]$KeepWwiseOpen
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

function Resolve-WaapiPython {
    param([string]$ExplicitPath)

    function Test-Candidate {
        param(
            [Parameter(Mandatory = $true)][string]$Path,
            [string[]]$PrefixArguments = @(),
            [Parameter(Mandatory = $true)][string]$Source,
            [switch]$DevelopmentFallback
        )

        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            return $null
        }
        $resolved = (Resolve-Path -LiteralPath $Path).Path
        $probeArguments = @($PrefixArguments) + @(
            '-c',
            'import json,sys,waapi; print(json.dumps({"executable":sys.executable,"version":sys.version.split()[0],"waapiModule":waapi.__file__}))'
        )
        $probeOutput = & $resolved @probeArguments 2>&1
        $probeExitCode = $LASTEXITCODE
        if ($probeExitCode -ne 0) {
            return $null
        }
        $probe = [string]($probeOutput | Select-Object -Last 1) | ConvertFrom-Json
        return [pscustomobject]@{
            Path                = $resolved
            PrefixArguments     = [string[]]$PrefixArguments
            Source              = $Source
            DevelopmentFallback = [bool]$DevelopmentFallback
            Executable          = [string]$probe.executable
            Version             = [string]$probe.version
            WaapiModule         = [string]$probe.waapiModule
        }
    }

    if ($ExplicitPath) {
        $candidate = Test-Candidate -Path $ExplicitPath -Source 'Parameter:-PythonWithWaapi'
        if (-not $candidate) {
            throw "-PythonWithWaapi must point to a Python executable that can import waapi-client: '$ExplicitPath'."
        }
        return $candidate
    }

    foreach ($environmentName in @('RWWA_WAAPI_PYTHON', 'WAAPI_PYTHON')) {
        $environmentPath = [System.Environment]::GetEnvironmentVariable($environmentName)
        if ($environmentPath) {
            $candidate = Test-Candidate -Path $environmentPath -Source "Environment:$environmentName"
            if (-not $candidate) {
                throw "$environmentName must point to a Python executable that can import waapi-client: '$environmentPath'."
            }
            return $candidate
        }
    }

    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python -and $python.Source -notlike '*\WindowsApps\python.exe') {
        $candidate = Test-Candidate -Path $python.Source -Source 'PATH:python.exe'
        if ($candidate) {
            return $candidate
        }
    }

    $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($launcher) {
        $candidate = Test-Candidate -Path $launcher.Source -PrefixArguments @('-3') -Source 'PATH:py.exe'
        if ($candidate) {
            return $candidate
        }
    }

    # This final path is a convenience for this development machine only. It is
    # smoke-test tooling and is never used by the product build or plug-in runtime.
    $localDevelopmentFallback = 'D:\Tool\Wwise_mcp\.venv\Scripts\python.exe'
    $candidate = Test-Candidate `
        -Path $localDevelopmentFallback `
        -Source 'LocalDevelopmentTestToolFallback' `
        -DevelopmentFallback
    if ($candidate) {
        return $candidate
    }

    throw 'No Python interpreter with waapi-client was found. Pass -PythonWithWaapi or set RWWA_WAAPI_PYTHON.'
}

function Test-TcpEndpoint {
    param(
        [Parameter(Mandatory = $true)][string]$HostName,
        [Parameter(Mandatory = $true)][int]$Port,
        [int]$TimeoutMilliseconds = 500
    )

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $asyncResult = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $asyncResult.AsyncWaitHandle.WaitOne($TimeoutMilliseconds)) {
            return $false
        }
        $client.EndConnect($asyncResult)
        return $true
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

function Invoke-PythonClient {
    param(
        [Parameter(Mandatory = $true)]$Python,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Python.Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @($Python.PrefixArguments) + $Arguments) {
        [void]$startInfo.ArgumentList.Add($argument)
    }

    $process = [System.Diagnostics.Process]::Start($startInfo)
    if (-not $process) {
        throw "Failed to start the WAAPI Python client with '$($Python.Path)'."
    }
    try {
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill($true)
            $process.WaitForExit()
            throw "The WAAPI Python client exceeded its $TimeoutSeconds second timeout."
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        [System.IO.File]::WriteAllText($StdoutPath, $stdout, [System.Text.UTF8Encoding]::new($false))
        [System.IO.File]::WriteAllText($StderrPath, $stderr, [System.Text.UTF8Encoding]::new($false))
        return $process.ExitCode
    }
    finally {
        $process.Dispose()
    }
}

function Get-DirectorySnapshot {
    param([Parameter(Mandatory = $true)][string]$Root)

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "Directory snapshot root is missing: '$Root'."
    }
    $resolvedRoot = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Root).Path).TrimEnd('\')
    $entries = @(
        Get-ChildItem -LiteralPath $resolvedRoot -File -Force -Recurse |
            Sort-Object -Property FullName |
            ForEach-Object {
                $relativePath = $_.FullName.Substring($resolvedRoot.Length).TrimStart('\').Replace('\', '/')
                $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                '{0}`t{1}`t{2}' -f $relativePath, $_.Length, $hash
            }
    )
    $manifestBytes = [System.Text.UTF8Encoding]::new($false).GetBytes([string]::Join("`n", $entries))
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = [System.Convert]::ToHexString($sha256.ComputeHash($manifestBytes)).ToLowerInvariant()
    }
    finally {
        $sha256.Dispose()
    }
    $totalBytes = [long]0
    Get-ChildItem -LiteralPath $resolvedRoot -File -Force -Recurse | ForEach-Object {
        $totalBytes += $_.Length
    }
    return [pscustomobject]@{
        Root         = $resolvedRoot
        FileCount    = $entries.Count
        TotalBytes   = $totalBytes
        DigestSha256 = $digest
        Entries      = [string[]]$entries
    }
}

function ConvertTo-SnapshotSummary {
    param([Parameter(Mandatory = $true)]$Snapshot)

    return [ordered]@{
        root         = [string]$Snapshot.Root
        fileCount    = [int]$Snapshot.FileCount
        totalBytes   = [long]$Snapshot.TotalBytes
        digestSha256 = [string]$Snapshot.DigestSha256
    }
}

function Test-DirectorySnapshotsEqual {
    param(
        [Parameter(Mandatory = $true)]$Reference,
        [Parameter(Mandatory = $true)]$Candidate
    )

    return $Reference.FileCount -eq $Candidate.FileCount -and
        $Reference.TotalBytes -eq $Candidate.TotalBytes -and
        $Reference.DigestSha256 -eq $Candidate.DigestSha256
}

$productRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Assert-PathUnderRoot `
    -Path (Join-Path $productRoot 'Build\WwiseSmoke') `
    -Root $productRoot `
    -Description 'Wwise smoke output directory'
[void](New-Item -ItemType Directory -Path $outputRoot -Force)

$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$reportPath = Join-Path $outputRoot "wwise-authoring-smoke-$runId.json"
$clientReportPath = Join-Path $outputRoot "wwise-authoring-smoke-$runId.client.json"
$capturePath = Join-Path $outputRoot "wwise-authoring-smoke-$runId.prof"
$clientStdoutPath = Join-Path $outputRoot "wwise-authoring-smoke-$runId.stdout.log"
$clientStderrPath = Join-Path $outputRoot "wwise-authoring-smoke-$runId.stderr.log"
$wwiseStdoutPath = Join-Path $outputRoot "wwise-authoring-smoke-$runId.wwise.stdout.log"
$wwiseStderrPath = Join-Path $outputRoot "wwise-authoring-smoke-$runId.wwise.stderr.log"

$report = [ordered]@{
    schemaVersion = 1
    tool          = 'RealWorldWeatherAcoustics.WwiseAuthoringSmokeWrapper'
    startedAtUtc  = [DateTime]::UtcNow.ToString('o')
    finishedAtUtc = $null
    success       = $false
    reportPath    = $reportPath
    preflight     = [ordered]@{
        productRoot       = $productRoot
        existingWwisePids = @()
        pluginArtifacts   = @()
        python            = $null
        effectClassId     = $EffectClassId
        effectInputWav    = $null
        requireRetainedRainDemo = [bool]$RequireRetainedRainDemo
        retainedRainDemo  = $null
    }
    launch        = [ordered]@{
        pid           = $null
        keepWwiseOpen = [bool]$KeepWwiseOpen
        waapiReady    = $false
        stoppedByWrapper = $false
        exitedNaturally  = $false
    }
    client        = $null
    outputs       = [ordered]@{
        profilerCapture = $capturePath
        clientStdout    = $clientStdoutPath
        clientStderr    = $clientStderrPath
        wwiseStdout     = $wwiseStdoutPath
        wwiseStderr     = $wwiseStderrPath
    }
    projectIsolation = [ordered]@{
        fixtureProjectRoot      = $null
        fixtureProjectPath      = $null
        fixtureBefore           = $null
        fixtureAfter            = $null
        fixtureUnchanged        = $null
        disposableRunRoot       = $null
        disposableProjectRoot   = $null
        disposableProjectPath   = $null
        disposableCopy          = $null
        copyMatchesFixtureBytes = $false
    }
    nativeHostFixture = [ordered]@{
        root       = $null
        files      = @()
        assertions = [ordered]@{}
    }
    cleanup       = [ordered]@{
        disposableProjectRemoved = $false
        disposableProjectRetained = $false
        errors = @()
    }
    error         = $null
}

$wwiseProcess = $null
$wwiseStopped = $false
$projectsRoot = $null
$disposableRunRoot = $null
$sourceSnapshotBefore = $null
$wrapperError = $null
$exitCode = 1

try {
    $environment = Resolve-WeatherEnvironment -WwiseRoot $WwiseRoot
    if (-not $environment.ProductRoot.Equals($productRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Common.ps1 resolved an unexpected product root: '$($environment.ProductRoot)'."
    }

    $effectInputWavCandidate = if ([string]::IsNullOrWhiteSpace($EffectInputWav)) {
        if ($RequireRetainedRainDemo) {
            Join-Path $productRoot 'WwiseSmoke\RealWorldWeatherAcousticsSmoke\Originals\SFX\RWWA_Heavy_Rain_Puddles_30s.wav'
        }
        else {
            Join-Path $productRoot 'Build\Core\Fixtures\hybrid_input.wav'
        }
    }
    else {
        $EffectInputWav
    }
    if (-not (Test-Path -LiteralPath $effectInputWavCandidate -PathType Leaf)) {
        throw "The Effect input WAV is missing: '$effectInputWavCandidate'."
    }
    $resolvedEffectInputWav = (Resolve-Path -LiteralPath $effectInputWavCandidate).Path
    if (-not [System.IO.Path]::GetExtension($resolvedEffectInputWav).Equals(
        '.wav',
        [System.StringComparison]::OrdinalIgnoreCase
    )) {
        throw "The Effect input file must be a WAV: '$resolvedEffectInputWav'."
    }
    $report.preflight.effectInputWav = $resolvedEffectInputWav

    $fixtureProjectPath = Assert-PathUnderRoot `
        -Path (Join-Path $productRoot 'WwiseSmoke\RealWorldWeatherAcousticsSmoke\RealWorldWeatherAcousticsSmoke.wproj') `
        -Root $productRoot `
        -Description 'Wwise smoke project'
    if (-not (Test-Path -LiteralPath $fixtureProjectPath -PathType Leaf)) {
        throw "The Wwise smoke fixture project is missing: '$fixtureProjectPath'."
    }
    $fixtureProjectRoot = Split-Path -Parent $fixtureProjectPath
    $report.projectIsolation.fixtureProjectRoot = $fixtureProjectRoot
    $report.projectIsolation.fixtureProjectPath = $fixtureProjectPath

    $demoSound = $null
    $demoAudioSource = $null
    $demoEffect = $null
    if ($RequireRetainedRainDemo) {
        $actorWorkUnitPath = Join-Path $fixtureProjectRoot 'Actor-Mixer Hierarchy\Default Work Unit.wwu'
        $eventWorkUnitPath = Join-Path $fixtureProjectRoot 'Events\Default Work Unit.wwu'
        if (-not (Test-Path -LiteralPath $actorWorkUnitPath -PathType Leaf) -or
            -not (Test-Path -LiteralPath $eventWorkUnitPath -PathType Leaf)) {
            throw 'The retained rain-demo Actor-Mixer or Event work unit is missing.'
        }
        [xml]$actorWorkUnit = Get-Content -LiteralPath $actorWorkUnitPath -Raw
        [xml]$eventWorkUnit = Get-Content -LiteralPath $eventWorkUnitPath -Raw
        $demoSound = $actorWorkUnit.SelectSingleNode("//Sound[@Name='RWWA_Demo_Heavy_Rain_Puddles']")
        $demoAudioSource = if ($demoSound) {
            $demoSound.SelectSingleNode(".//AudioFileSource[@Name='RWWA_Demo_Heavy_Rain_Puddles_Audio']")
        }
        else { $null }
        $demoEffect = if ($demoSound) {
            $demoSound.SelectSingleNode(".//Effect[@Name='RWWA_Demo_Weather_Geometry_Effect']")
        }
        else { $null }
        $demoEvent = $eventWorkUnit.SelectSingleNode("//Event[@Name='Play_RWWA_Demo_Heavy_Rain_Puddles']")
        $demoEventTarget = if ($demoEvent) {
            $demoEvent.SelectSingleNode(".//Reference[@Name='Target']/ObjectRef[@Name='RWWA_Demo_Heavy_Rain_Puddles']")
        }
        else { $null }
        $demoAssertions = [ordered]@{
            soundPersisted           = $null -ne $demoSound
            audioFileSourcePersisted = $null -ne $demoAudioSource
            audioFileMatchesInput    = ($null -ne $demoAudioSource -and
                $demoAudioSource.SelectSingleNode('./AudioFile').'#text' -eq
                'RWWA_Heavy_Rain_Puddles_30s.wav')
            effectPersisted          = ($null -ne $demoEffect -and
                [string]$demoEffect.CompanyID -eq '64' -and
                [string]$demoEffect.PluginID -eq '31002')
            eventPersisted           = $null -ne $demoEvent
            eventTargetsSound        = $null -ne $demoEventTarget
        }
        $report.preflight.retainedRainDemo = [ordered]@{
            actorWorkUnit = $actorWorkUnitPath
            eventWorkUnit = $eventWorkUnitPath
            soundId       = if ($demoSound) { [string]$demoSound.ID } else { $null }
            audioSourceId = if ($demoAudioSource) { [string]$demoAudioSource.ID } else { $null }
            effectId      = if ($demoEffect) { [string]$demoEffect.ID } else { $null }
            assertions    = $demoAssertions
        }
        $failedDemoAssertions = @(
            $demoAssertions.GetEnumerator() |
                Where-Object { -not [bool]$_.Value } |
                ForEach-Object { $_.Key }
        )
        if ($failedDemoAssertions.Count -gt 0) {
            throw "The retained rain demo is incomplete: $($failedDemoAssertions -join ', ')."
        }
    }

    $wwiseExecutable = Join-Path $environment.WwiseRoot 'Authoring\x64\Release\bin\Wwise.exe'
    if (-not (Test-Path -LiteralPath $wwiseExecutable -PathType Leaf)) {
        throw "Wwise Authoring executable is missing: '$wwiseExecutable'."
    }

    $manifestPath = Join-Path $environment.ArtifactsRoot 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Installation manifest is missing: '$manifestPath'."
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $installEntries = @($manifest.entries | Where-Object { $_.install })
    if ($installEntries.Count -ne 2) {
        throw "Expected exactly two Authoring installation entries, found $($installEntries.Count)."
    }
    $stageRecordPath = Join-Path $environment.ArtifactsRoot 'stage-record.json'
    $stageRecord = if (Test-Path -LiteralPath $stageRecordPath -PathType Leaf) {
        Get-Content -LiteralPath $stageRecordPath -Raw | ConvertFrom-Json
    }
    else {
        $null
    }

    $artifactChecks = @()
    foreach ($entry in $installEntries) {
        $installedPath = Assert-PathUnderRoot `
            -Path (Join-Path $environment.WwiseRoot ([string]$entry.install).Replace('{configuration}', 'Release')) `
            -Root $environment.WwiseRoot `
            -Description "Installed file for '$($entry.id)'"
        if (-not (Test-Path -LiteralPath $installedPath -PathType Leaf)) {
            throw "Installed Authoring plug-in file is missing: '$installedPath'. Run Scripts\Install-WwiseAuthoring.ps1 -Apply after staging."
        }
        if ((Get-Item -LiteralPath $installedPath).Length -eq 0) {
            throw "Installed Authoring plug-in file is empty: '$installedPath'."
        }

        $installedHash = (Get-FileHash -LiteralPath $installedPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $stagedPath = Assert-PathUnderRoot `
            -Path (Join-Path $productRoot ([string]$entry.stage).Replace('{configuration}', 'Release')) `
            -Root $productRoot `
            -Description "Staged file for '$($entry.id)'"
        $stagedExists = Test-Path -LiteralPath $stagedPath -PathType Leaf
        $stagedHash = if ($stagedExists) {
            (Get-FileHash -LiteralPath $stagedPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        else {
            $null
        }
        $hashMatchesStage = if ($stagedExists) { $installedHash -eq $stagedHash } else { $null }
        if ($stagedExists -and -not $hashMatchesStage) {
            throw "Installed '$($entry.id)' does not match the staged Artifact hash. Reinstall before running the smoke test."
        }

        $stageRecordHash = $null
        $hashMatchesStageRecord = $null
        if ($stageRecord) {
            $recordEntry = @($stageRecord.entries | Where-Object { $_.id -eq $entry.id })
            if ($recordEntry.Count -eq 1) {
                $stageRecordHash = [string]$recordEntry[0].sha256
                $hashMatchesStageRecord = $installedHash -eq $stageRecordHash
                if (-not $hashMatchesStageRecord) {
                    throw "Installed '$($entry.id)' does not match stage-record.json. Reinstall before running the smoke test."
                }
            }
        }

        $artifactChecks += [pscustomobject]@{
            id                     = [string]$entry.id
            installedPath          = $installedPath
            installedSha256        = $installedHash
            stagedPath             = $stagedPath
            stagedExists           = $stagedExists
            stagedSha256           = $stagedHash
            hashMatchesStage       = $hashMatchesStage
            stageRecordSha256      = $stageRecordHash
            hashMatchesStageRecord = $hashMatchesStageRecord
        }
    }
    $report.preflight.pluginArtifacts = $artifactChecks

    $existingWwise = @(Get-Process -Name 'Wwise' -ErrorAction SilentlyContinue)
    $report.preflight.existingWwisePids = @($existingWwise | ForEach-Object { $_.Id })
    if ($existingWwise.Count -gt 0) {
        throw "Wwise Authoring is already running (PID(s): $($report.preflight.existingWwisePids -join ', ')). The smoke wrapper will not attach to or take over a user session."
    }

    $python = Resolve-WaapiPython -ExplicitPath $PythonWithWaapi
    $report.preflight.python = $python
    if ($python.DevelopmentFallback) {
        Write-Warning "Using '$($python.Path)' as a local development smoke-test fallback only; the product build does not depend on it."
    }

    $waapiUri = [System.Uri]$WaapiUrl
    if ($waapiUri.Scheme -notin @('ws', 'wss') -or $waapiUri.Port -le 0) {
        throw "-WaapiUrl must be a ws:// or wss:// URL with a valid port: '$WaapiUrl'."
    }

    $sourceSnapshotBefore = Get-DirectorySnapshot -Root $fixtureProjectRoot
    $report.projectIsolation.fixtureBefore = ConvertTo-SnapshotSummary -Snapshot $sourceSnapshotBefore
    $projectsRoot = Assert-PathUnderRoot `
        -Path (Join-Path $outputRoot 'Projects') `
        -Root $productRoot `
        -Description 'Disposable Wwise smoke projects directory'
    [void](New-Item -ItemType Directory -Path $projectsRoot -Force)
    $disposableRunRoot = Assert-PathUnderRoot `
        -Path (Join-Path $projectsRoot $runId) `
        -Root $projectsRoot `
        -Description 'Disposable Wwise smoke run directory'
    if (Test-Path -LiteralPath $disposableRunRoot) {
        throw "Disposable Wwise smoke run directory already exists: '$disposableRunRoot'."
    }
    [void](New-Item -ItemType Directory -Path $disposableRunRoot)
    Copy-Item -LiteralPath $fixtureProjectRoot -Destination $disposableRunRoot -Recurse -Force

    $disposableProjectRoot = Join-Path $disposableRunRoot (Split-Path -Leaf $fixtureProjectRoot)
    $projectPath = Join-Path $disposableProjectRoot (Split-Path -Leaf $fixtureProjectPath)
    if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
        throw "Disposable Wwise smoke project copy is missing: '$projectPath'."
    }
    $copySnapshot = Get-DirectorySnapshot -Root $disposableProjectRoot
    $copyMatchesFixtureBytes = Test-DirectorySnapshotsEqual `
        -Reference $sourceSnapshotBefore `
        -Candidate $copySnapshot
    $report.projectIsolation.disposableRunRoot = $disposableRunRoot
    $report.projectIsolation.disposableProjectRoot = $disposableProjectRoot
    $report.projectIsolation.disposableProjectPath = $projectPath
    $report.projectIsolation.disposableCopy = ConvertTo-SnapshotSummary -Snapshot $copySnapshot
    $report.projectIsolation.copyMatchesFixtureBytes = $copyMatchesFixtureBytes
    if (-not $copyMatchesFixtureBytes) {
        throw 'Disposable Wwise project copy does not match the source fixture byte-for-byte.'
    }

    $wwiseProcess = Start-Process `
        -FilePath $wwiseExecutable `
        -ArgumentList @("`"$projectPath`"") `
        -WindowStyle Normal `
        -RedirectStandardOutput $wwiseStdoutPath `
        -RedirectStandardError $wwiseStderrPath `
        -PassThru
    $report.launch.pid = $wwiseProcess.Id

    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $wwiseProcess.Refresh()
        if ($wwiseProcess.HasExited) {
            throw "The Wwise process started by the wrapper exited with code $($wwiseProcess.ExitCode) before WAAPI became ready."
        }
        if (Test-TcpEndpoint -HostName $waapiUri.Host -Port $waapiUri.Port) {
            $report.launch.waapiReady = $true
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $report.launch.waapiReady) {
        throw "WAAPI did not listen at '$WaapiUrl' within $StartupTimeoutSeconds seconds. Ensure Wwise Authoring API is enabled."
    }

    $clientPath = Join-Path $productRoot 'Tools\WwiseSmoke\wwise_authoring_smoke.py'
    if (-not (Test-Path -LiteralPath $clientPath -PathType Leaf)) {
        throw "WAAPI smoke client is missing: '$clientPath'."
    }
    $nativeHostFixtureRoot = Assert-PathUnderRoot `
        -Path (Join-Path $productRoot "Build\NativeHost\Fixture\$runId") `
        -Root $productRoot `
        -Description 'retained Native Host fixture directory'
    $clientArguments = @(
        $clientPath,
        '--project', $projectPath,
        '--report', $clientReportPath,
        '--capture-file', $capturePath,
        '--waapi-url', $WaapiUrl,
        '--connect-timeout', '30',
        '--playback-seconds', $PlaybackSeconds.ToString([System.Globalization.CultureInfo]::InvariantCulture),
        '--silence-floor-db', $SilenceFloorDb.ToString([System.Globalization.CultureInfo]::InvariantCulture),
        '--source-class-id', '2031682562',
        '--effect-class-id', $EffectClassId.ToString([System.Globalization.CultureInfo]::InvariantCulture),
        '--effect-input-wav', $resolvedEffectInputWav,
        '--native-host-fixture-dir', $nativeHostFixtureRoot,
        '--wwise-pid', $wwiseProcess.Id.ToString([System.Globalization.CultureInfo]::InvariantCulture),
        '--gui-timeout', '20'
    )
    if ($RequireRetainedRainDemo) {
        $clientArguments += @(
            '--retained-effect-sound-id', ([string]$demoSound.ID),
            '--retained-effect-audio-source-id', ([string]$demoAudioSource.ID),
            '--retained-effect-id', ([string]$demoEffect.ID)
        )
    }
    $clientTimeout = [Math]::Max(90, [int][Math]::Ceiling($PlaybackSeconds + 70))
    $clientExitCode = Invoke-PythonClient `
        -Python $python `
        -Arguments $clientArguments `
        -StdoutPath $clientStdoutPath `
        -StderrPath $clientStderrPath `
        -TimeoutSeconds $clientTimeout
    if (Test-Path -LiteralPath $clientReportPath -PathType Leaf) {
        $report.client = Get-Content -LiteralPath $clientReportPath -Raw | ConvertFrom-Json
        Remove-Item -LiteralPath $clientReportPath -Force
    }
    if ($clientExitCode -ne 0) {
        $clientMessage = if ($report.client -and $report.client.error) {
            [string]$report.client.error.message
        }
        else {
            "See '$clientStderrPath'."
        }
        throw "WAAPI smoke client failed with exit code ${clientExitCode}: $clientMessage"
    }
    if (-not $report.client -or -not $report.client.success) {
        throw 'WAAPI smoke client did not produce a successful structured report.'
    }

    $reportedFixture = $report.client.nativeHostFixture
    if (-not $reportedFixture) {
        throw 'WAAPI smoke client did not report retained Native Host fixture artifacts.'
    }
    if (-not ([string]$reportedFixture.root).Equals(
        $nativeHostFixtureRoot,
        [System.StringComparison]::OrdinalIgnoreCase
    )) {
        throw "WAAPI smoke client retained Native Host artifacts at an unexpected root: '$($reportedFixture.root)'."
    }
    $failedFixtureAssertions = @(
        $reportedFixture.assertions.PSObject.Properties |
            Where-Object { -not [bool]$_.Value } |
            ForEach-Object { $_.Name }
    )
    if ($failedFixtureAssertions.Count -gt 0) {
        throw "Native Host fixture assertions failed: $($failedFixtureAssertions -join ', ')."
    }
    foreach ($fixtureFile in @($reportedFixture.files)) {
        $fixturePath = Assert-PathUnderRoot `
            -Path ([string]$fixtureFile.path) `
            -Root $nativeHostFixtureRoot `
            -Description 'retained Native Host fixture artifact'
        if (-not (Test-Path -LiteralPath $fixturePath -PathType Leaf)) {
            throw "Retained Native Host fixture artifact is missing: '$fixturePath'."
        }
        $actualFixtureHash = (Get-FileHash -LiteralPath $fixturePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if (-not $actualFixtureHash.Equals(
            [string]$fixtureFile.sha256,
            [System.StringComparison]::OrdinalIgnoreCase
        )) {
            throw "Retained Native Host fixture artifact hash mismatch: '$fixturePath'."
        }
    }
    $report.nativeHostFixture = $reportedFixture

    $exitCode = 0
}
catch {
    $wrapperError = $_
    $report.error = [ordered]@{
        type       = $_.Exception.GetType().FullName
        message    = $_.Exception.Message
        scriptLine = $_.InvocationInfo.ScriptLineNumber
        position   = $_.InvocationInfo.PositionMessage
    }
    $exitCode = 1
}
finally {
    if ($wwiseProcess) {
        try {
            $wwiseProcess.Refresh()
            if ($wwiseProcess.HasExited) {
                $report.launch.exitedNaturally = $true
                $wwiseStopped = $true
            }
            elseif (-not $KeepWwiseOpen) {
                # Stop only the exact process object returned by Start-Process above.
                Stop-Process -Id $wwiseProcess.Id -ErrorAction Stop
                if (-not $wwiseProcess.WaitForExit(10000)) {
                    throw "Wwise PID $($wwiseProcess.Id) did not exit within the cleanup timeout."
                }
                $report.launch.stoppedByWrapper = $true
                $wwiseStopped = $true
            }
        }
        catch {
            $report.cleanup.errors += "Failed to clean up wrapper-owned Wwise PID $($wwiseProcess.Id): $($_.Exception.Message)"
            $exitCode = 1
        }
        finally {
            $wwiseProcess.Dispose()
        }
    }
    else {
        $wwiseStopped = $true
    }

    if ($sourceSnapshotBefore) {
        try {
            $sourceSnapshotAfter = Get-DirectorySnapshot -Root $sourceSnapshotBefore.Root
            $fixtureUnchanged = Test-DirectorySnapshotsEqual `
                -Reference $sourceSnapshotBefore `
                -Candidate $sourceSnapshotAfter
            $report.projectIsolation.fixtureAfter = ConvertTo-SnapshotSummary -Snapshot $sourceSnapshotAfter
            $report.projectIsolation.fixtureUnchanged = $fixtureUnchanged
            if (-not $fixtureUnchanged) {
                throw 'The tracked Wwise smoke fixture changed during the disposable smoke run.'
            }
        }
        catch {
            $report.cleanup.errors += "Fixture integrity verification failed: $($_.Exception.Message)"
            $exitCode = 1
        }
    }

    if ($disposableRunRoot -and (Test-Path -LiteralPath $disposableRunRoot)) {
        if ($wwiseStopped) {
            try {
                $resolvedProjectsRoot = [System.IO.Path]::GetFullPath($projectsRoot).TrimEnd('\')
                $resolvedDisposableRoot = [System.IO.Path]::GetFullPath($disposableRunRoot)
                if (-not $resolvedDisposableRoot.StartsWith(
                    $resolvedProjectsRoot + '\',
                    [System.StringComparison]::OrdinalIgnoreCase
                )) {
                    throw "Refusing to remove unexpected disposable project path '$resolvedDisposableRoot'."
                }
                Remove-Item -LiteralPath $resolvedDisposableRoot -Recurse -Force
                $report.cleanup.disposableProjectRemoved = -not (
                    Test-Path -LiteralPath $resolvedDisposableRoot
                )
                if (-not $report.cleanup.disposableProjectRemoved) {
                    throw "Disposable Wwise project still exists after cleanup: '$resolvedDisposableRoot'."
                }
            }
            catch {
                $report.cleanup.errors += "Disposable project cleanup failed: $($_.Exception.Message)"
                $exitCode = 1
            }
        }
        else {
            $report.cleanup.disposableProjectRetained = $true
        }
    }

    if ($report.cleanup.errors.Count -gt 0) {
        if (-not $report.error) {
            $report.error = [ordered]@{
                type       = 'WwiseSmokeCleanupFailure'
                message    = [string]::Join('; ', [string[]]$report.cleanup.errors)
                scriptLine = $null
                position   = $null
            }
        }
        else {
            $report.error['cleanupErrors'] = [string[]]$report.cleanup.errors
        }
    }

    $report.success = ($exitCode -eq 0)
    $report.finishedAtUtc = [DateTime]::UtcNow.ToString('o')
    $json = $report | ConvertTo-Json -Depth 40
    [System.IO.File]::WriteAllText($reportPath, $json + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
    Write-Host "Wwise Authoring smoke report: '$reportPath'"
}

exit $exitCode
