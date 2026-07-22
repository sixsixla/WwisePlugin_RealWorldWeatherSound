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
            '-DRWWA_BUILD_OFFLINE_RENDERER=ON',
            '-DRWWA_BUILD_WWISE_BANK_TESTS=ON',
            "-DRWWA_WWISE_SDK_ROOT=$($environment.WwiseRoot)"
        ) -Description 'Configure shared Core, Wwise bank-contract tests, and offline renderer'
        Invoke-CheckedCommand -FilePath $environment.CMakePath -ArgumentList @(
            '--build', $coreBuild,
            '--config', $Configuration,
            '--parallel'
        ) -Description 'Build shared Core, Wwise bank-contract tests, and offline renderer'
    }
    else {
        Write-Warning "No root CMakeLists.txt exists yet; shared Core/tests/offline renderer were skipped."
    }
}

if (-not $SkipWwise) {
    Assert-WwiseProjectIsolation -PluginRoot $environment.PluginRoot
    $projects = Get-WwiseGeneratedProjects -PluginRoot $environment.PluginRoot
    $runtimeStaticProject = $projects[0]
    $runtimeSharedProject = $projects[1]
    $authoringProject = $projects[2]

    function Invoke-WwiseMsBuild {
        param(
            [Parameter(Mandatory = $true)][string]$Project,
            [Parameter(Mandatory = $true)][string]$BuildConfiguration,
            [Parameter(Mandatory = $true)][string]$Description
        )

        Invoke-CheckedCommand -FilePath $environment.MSBuildPath -ArgumentList @(
            $Project,
            '/nologo',
            '/m',
            '/t:Build',
            "/p:Configuration=$BuildConfiguration",
            '/p:Platform=x64',
            '/verbosity:minimal'
        ) -Description $Description
    }

    # wp.py remains the canonical project generator (Scripts\Configure.ps1),
    # but its build subcommand decodes vswhere output through Python's locale.
    # That is unreliable on Chinese Windows when vswhere emits GBK. Calling the
    # generated vc170 projects with the already-resolved VS2022 MSBuild avoids
    # the locale boundary and builds every dependency explicitly.
    Invoke-WwiseMsBuild `
        -Project $runtimeStaticProject `
        -BuildConfiguration 'Release(StaticCRT)' `
        -Description 'Build Wwise Runtime static-CRT dependency (Windows x64 Release)'
    Invoke-WwiseMsBuild `
        -Project $runtimeStaticProject `
        -BuildConfiguration 'Profile' `
        -Description 'Build Wwise Runtime Profile dependency for Authoring (Windows x64)'
    Invoke-WwiseMsBuild `
        -Project $runtimeStaticProject `
        -BuildConfiguration $Configuration `
        -Description 'Build Wwise Runtime static plug-in (Windows x64 Release)'
    Invoke-WwiseMsBuild `
        -Project $runtimeSharedProject `
        -BuildConfiguration $Configuration `
        -Description 'Build Wwise Runtime shared plug-in (Windows x64 Release)'
    Invoke-WwiseMsBuild `
        -Project $authoringProject `
        -BuildConfiguration $Configuration `
        -Description 'Build Wwise Authoring plug-in (Windows x64 Release)'

    Assert-ExpectedBuildOutputs -ProductRoot $environment.ProductRoot -Configuration $Configuration
}

Write-Host 'Build complete. Full outputs remain under Build\ and the factory header is under Artifacts\Runtime.'
