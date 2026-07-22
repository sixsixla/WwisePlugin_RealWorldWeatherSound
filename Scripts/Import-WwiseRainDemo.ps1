[CmdletBinding()]
param(
    [string]$WwiseRoot = 'E:\WwiseSoft2023\Wwise_2023.1.19.8928',
    [string]$PythonWithWaapi = 'D:\Tool\Wwise_mcp\.venv\Scripts\python.exe',
    [string]$WaapiUrl = 'ws://127.0.0.1:8080/waapi',
    [ValidateRange(10, 300)][int]$StartupTimeoutSeconds = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

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

$productRoot = Get-ProductRoot
$environment = Resolve-WeatherEnvironment -WwiseRoot $WwiseRoot
$fixtureRoot = Join-Path $productRoot 'WwiseSmoke\RealWorldWeatherAcousticsSmoke'
$fixtureProject = Join-Path $fixtureRoot 'RealWorldWeatherAcousticsSmoke.wproj'
$fixtureWav = Join-Path $fixtureRoot 'Originals\SFX\RWWA_Heavy_Rain_Puddles_30s.wav'
$clientScript = Join-Path $productRoot 'Tools\WwiseSmoke\import_rain_demo.py'
$wwiseExecutable = Join-Path $environment.WwiseRoot 'Authoring\x64\Release\bin\Wwise.exe'

foreach ($requiredPath in @($fixtureProject, $fixtureWav, $clientScript, $wwiseExecutable, $PythonWithWaapi)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required rain-demo import file is missing: '$requiredPath'."
    }
}

$existingWwise = @(Get-Process -Name Wwise -ErrorAction SilentlyContinue)
if ($existingWwise.Count -gt 0) {
    throw "Close Wwise before importing the retained demo. Running PIDs: $($existingWwise.Id -join ', ')."
}

$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$outputRoot = Assert-PathUnderRoot `
    -Path (Join-Path $productRoot 'Build\WwiseSmoke') `
    -Root $productRoot `
    -Description 'Wwise smoke output root'
$importRoot = Assert-PathUnderRoot `
    -Path (Join-Path $outputRoot "PersistentImport\$runId") `
    -Root $outputRoot `
    -Description 'Persistent import working root'
[void](New-Item -ItemType Directory -Path $importRoot -Force)
Copy-Item -LiteralPath $fixtureRoot -Destination $importRoot -Recurse -Force

$workingFixtureRoot = Join-Path $importRoot (Split-Path -Leaf $fixtureRoot)
$workingProject = Join-Path $workingFixtureRoot (Split-Path -Leaf $fixtureProject)
$workingWav = Join-Path $workingFixtureRoot 'Originals\SFX\RWWA_Heavy_Rain_Puddles_30s.wav'
$reportPath = Join-Path $outputRoot "import-rain-demo-$runId.json"
$stdoutPath = Join-Path $outputRoot "import-rain-demo-$runId.stdout.log"
$stderrPath = Join-Path $outputRoot "import-rain-demo-$runId.stderr.log"
$wwiseStdoutPath = Join-Path $outputRoot "import-rain-demo-$runId.wwise.stdout.log"
$wwiseStderrPath = Join-Path $outputRoot "import-rain-demo-$runId.wwise.stderr.log"

$waapiUri = [System.Uri]$WaapiUrl
if ($waapiUri.Scheme -notin @('ws', 'wss') -or $waapiUri.Port -le 0) {
    throw "-WaapiUrl must be a ws:// or wss:// URL with a valid port: '$WaapiUrl'."
}

$wwiseProcess = $null
try {
    $wwiseProcess = Start-Process `
        -FilePath $wwiseExecutable `
        -ArgumentList @("`"$workingProject`"") `
        -WindowStyle Hidden `
        -RedirectStandardOutput $wwiseStdoutPath `
        -RedirectStandardError $wwiseStderrPath `
        -PassThru

    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $wwiseProcess.Refresh()
        if ($wwiseProcess.HasExited) {
            throw "Wwise exited with code $($wwiseProcess.ExitCode) before WAAPI became ready."
        }
        if (Test-TcpEndpoint -HostName $waapiUri.Host -Port $waapiUri.Port) {
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not (Test-TcpEndpoint -HostName $waapiUri.Host -Port $waapiUri.Port)) {
        throw "WAAPI did not become ready at '$WaapiUrl' within $StartupTimeoutSeconds seconds."
    }

    $arguments = @(
        $clientScript,
        '--project', $workingProject,
        '--wav', $workingWav,
        '--report', $reportPath,
        '--waapi-url', $WaapiUrl,
        '--connect-timeout', '30'
    )
    $output = & $PythonWithWaapi @arguments 2>&1
    $clientExitCode = $LASTEXITCODE
    [System.IO.File]::WriteAllLines($stdoutPath, @($output), [System.Text.UTF8Encoding]::new($false))
    if ($clientExitCode -ne 0) {
        throw "Rain-demo WAAPI client failed with exit code $clientExitCode. Report: '$reportPath'."
    }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (-not $report.success) {
        throw "Rain-demo WAAPI client did not report success: '$reportPath'."
    }
}
finally {
    if ($wwiseProcess) {
        $wwiseProcess.Refresh()
        if (-not $wwiseProcess.HasExited) {
            Stop-Process -Id $wwiseProcess.Id -ErrorAction Stop
            if (-not $wwiseProcess.WaitForExit(10000)) {
                throw "Wwise PID $($wwiseProcess.Id) did not exit within 10 seconds."
            }
        }
        $wwiseProcess.Dispose()
    }
}

$relativeWorkUnits = @(
    'Actor-Mixer Hierarchy\Default Work Unit.wwu',
    'Events\Default Work Unit.wwu'
)
$copiedWorkUnits = @()
foreach ($relativePath in $relativeWorkUnits) {
    $source = Assert-PathUnderRoot `
        -Path (Join-Path $workingFixtureRoot $relativePath) `
        -Root $workingFixtureRoot `
        -Description 'Imported Wwise work unit'
    $destination = Assert-PathUnderRoot `
        -Path (Join-Path $fixtureRoot $relativePath) `
        -Root $fixtureRoot `
        -Description 'Retained Wwise work unit'
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Imported Wwise work unit is missing: '$source'."
    }
    Copy-Item -LiteralPath $source -Destination $destination -Force
    $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    if (-not $sourceHash.Equals($destinationHash, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Retained Wwise work unit hash mismatch: '$destination'."
    }
    $copiedWorkUnits += [pscustomobject]@{
        path = $destination
        sha256 = $destinationHash
        bytes = (Get-Item -LiteralPath $destination).Length
    }
}

[pscustomobject]@{
    success = $true
    report = $reportPath
    workingProject = $workingProject
    retainedProject = $fixtureProject
    retainedWav = $fixtureWav
    copiedWorkUnits = $copiedWorkUnits
} | ConvertTo-Json -Depth 5
