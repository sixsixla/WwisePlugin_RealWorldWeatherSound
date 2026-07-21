[CmdletBinding()]
param(
    [string]$WwiseRoot,
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [ValidateSet('vc170')][string]$Toolset = 'vc170',
    [switch]$SkipCore,
    [switch]$SkipWwise
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

if ($SkipCore -and $SkipWwise) {
    throw 'Both -SkipCore and -SkipWwise were specified; there is nothing to build.'
}

$resolveArguments = @{ WwiseRoot = $WwiseRoot }
if (-not $SkipCore) {
    $resolveArguments.RequireCMake = $true
}
if (-not $SkipWwise) {
    $resolveArguments.RequirePython = $true
    $resolveArguments.RequireMSBuild = $true
}
$environment = Resolve-WeatherEnvironment @resolveArguments

if (-not $SkipCore) {
    $cmakeProject = Join-Path $environment.ProductRoot 'CMakeLists.txt'
    if (Test-Path -LiteralPath $cmakeProject -PathType Leaf) {
        $coreBuild = Assert-PathUnderRoot -Path (Join-Path $environment.BuildRoot 'Core') -Root $environment.ProductRoot -Description 'Core build directory'
        Invoke-CheckedCommand -FilePath $environment.CMakePath -ArgumentList @(
            '-S', $environment.ProductRoot,
            '-B', $coreBuild,
            '-A', 'x64',
            '-DBUILD_TESTING=ON',
            '-DRWWA_BUILD_TESTS=ON',
            '-DRWWA_BUILD_OFFLINE_RENDERER=ON'
        ) -Description 'Configure shared Core, tests, and offline renderer'
        Invoke-CheckedCommand -FilePath $environment.CMakePath -ArgumentList @(
            '--build', $coreBuild,
            '--config', $Configuration,
            '--parallel'
        ) -Description 'Build shared Core, tests, and offline renderer'
    }
    else {
        Write-Warning "No root CMakeLists.txt exists yet; shared Core/tests/offline renderer were skipped."
    }
}

if (-not $SkipWwise) {
    Assert-WwiseProjectIsolation -PluginRoot $environment.PluginRoot
    Push-Location $environment.PluginRoot
    try {
        Invoke-Wp -Environment $environment -ArgumentList @(
            'build', 'Windows_vc170', '-c', 'Release(StaticCRT)', '-x', 'x64', '-t', $Toolset
        ) -Description 'Build Wwise Runtime static-CRT dependency (Windows x64 Release)'
        Invoke-Wp -Environment $environment -ArgumentList @(
            'build', 'Windows_vc170', '-c', $Configuration, '-x', 'x64', '-t', $Toolset
        ) -Description 'Build Wwise Runtime plug-in (Windows x64 Release)'
        Invoke-Wp -Environment $environment -ArgumentList @(
            'build', 'Authoring_Windows', '-c', $Configuration, '-x', 'x64', '-t', $Toolset
        ) -Description 'Build Wwise Authoring plug-in (Windows x64 Release)'
    }
    finally {
        Pop-Location
    }

    Assert-ExpectedBuildOutputs -ProductRoot $environment.ProductRoot -Configuration $Configuration
}

Write-Host 'Build complete. Full outputs remain under Build\ and the factory header is under Artifacts\Runtime.'
