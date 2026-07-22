Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-ProductRoot {
    return [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
}

function Assert-PathUnderRoot {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    if (-not $pathFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description must remain under '$Root', but resolved to '$pathFull'."
    }

    return $pathFull
}

function Resolve-WwiseRoot {
    param([string]$WwiseRoot)

    $candidates = New-Object 'System.Collections.Generic.List[string]'
    if ($WwiseRoot) {
        $candidates.Add($WwiseRoot)
    }
    else {
        foreach ($candidate in @($env:WWISE_ROOT, $env:WWISEROOT, 'E:\WwiseSoft2023\Wwise_2023.1.19.8928')) {
            if ($candidate) {
                $candidates.Add($candidate)
            }
        }

        foreach ($parent in @('E:\WwiseSoft2023', (Join-Path ${env:ProgramFiles(x86)} 'Audiokinetic'))) {
            if (Test-Path -LiteralPath $parent -PathType Container) {
                Get-ChildItem -LiteralPath $parent -Directory -ErrorAction SilentlyContinue |
                    Where-Object { $_.Name -like 'Wwise*' } |
                    Sort-Object Name -Descending |
                    ForEach-Object { $candidates.Add($_.FullName) }
            }
        }
    }

    foreach ($candidate in $candidates) {
        $wp = Join-Path $candidate 'Scripts\Build\Plugins\wp.py'
        $versionHeader = Join-Path $candidate 'SDK\include\AK\AkWwiseSDKVersion.h'
        if ((Test-Path -LiteralPath $wp -PathType Leaf) -and (Test-Path -LiteralPath $versionHeader -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    if ($WwiseRoot) {
        throw "The explicit Wwise root '$WwiseRoot' does not contain Scripts\Build\Plugins\wp.py and SDK\include\AK\AkWwiseSDKVersion.h."
    }

    throw 'Wwise was not found. Pass -WwiseRoot or set WWISE_ROOT.'
}

function Get-WwiseVersion {
    param([Parameter(Mandatory = $true)][string]$WwiseRoot)

    $header = Join-Path $WwiseRoot 'SDK\include\AK\AkWwiseSDKVersion.h'
    $content = Get-Content -Raw -LiteralPath $header
    $parts = @()
    foreach ($name in @('MAJOR', 'MINOR', 'SUBMINOR', 'BUILD')) {
        $match = [regex]::Match($content, "(?m)^#define\s+AK_WWISESDK_VERSION_$name\s+(\d+)\s*$")
        if (-not $match.Success) {
            throw "Could not read AK_WWISESDK_VERSION_$name from '$header'."
        }
        $parts += $match.Groups[1].Value
    }

    return ($parts -join '.')
}

function Resolve-PythonInvocation {
    if ($env:WEATHER_PYTHON) {
        if (-not (Test-Path -LiteralPath $env:WEATHER_PYTHON -PathType Leaf)) {
            throw "WEATHER_PYTHON points to a missing file: '$env:WEATHER_PYTHON'."
        }
        return [pscustomobject]@{ Path = (Resolve-Path -LiteralPath $env:WEATHER_PYTHON).Path; PrefixArguments = @() }
    }

    $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($launcher) {
        return [pscustomobject]@{ Path = $launcher.Source; PrefixArguments = @('-3') }
    }

    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python -and $python.Source -notlike '*\WindowsApps\python.exe') {
        return [pscustomobject]@{ Path = $python.Source; PrefixArguments = @() }
    }

    throw 'Python 3 was not found. Install Python 3, set WEATHER_PYTHON, or make py.exe available.'
}

function Resolve-RequiredCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Purpose
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "$Name is required for $Purpose but was not found on PATH."
    }
    return $command.Source
}

function Resolve-MSBuildPath {
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) {
        throw "vswhere.exe was not found at '$vsWhere'. Install Visual Studio 2022 with MSBuild and C++ tools."
    }

    $result = & $vsWhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe'
    if ($LASTEXITCODE -ne 0 -or -not $result) {
        throw 'Visual Studio 2022 MSBuild was not found.'
    }

    return [string]($result | Select-Object -First 1)
}

function Resolve-WeatherEnvironment {
    param(
        [string]$WwiseRoot,
        [switch]$RequirePython,
        [switch]$RequireCMake,
        [switch]$RequireCTest,
        [switch]$RequireMSBuild
    )

    $productRoot = Get-ProductRoot
    $resolvedWwiseRoot = Resolve-WwiseRoot -WwiseRoot $WwiseRoot
    $wwiseVersion = Get-WwiseVersion -WwiseRoot $resolvedWwiseRoot
    $expectedVersion = '2023.1.19.8928'
    if ($wwiseVersion -ne $expectedVersion) {
        throw "This v1 build is ABI-locked to Wwise $expectedVersion; '$resolvedWwiseRoot' is Wwise $wwiseVersion."
    }

    $python = $null
    if ($RequirePython) {
        $python = Resolve-PythonInvocation
    }

    return [pscustomobject]@{
        ProductRoot          = $productRoot
        PluginRoot           = Join-Path $productRoot 'RealWorldWeatherAcoustics'
        BuildRoot            = Join-Path $productRoot 'Build'
        ArtifactsRoot        = Join-Path $productRoot 'Artifacts'
        WwiseRoot            = $resolvedWwiseRoot
        WwiseVersion         = $wwiseVersion
        WpPy                 = Join-Path $resolvedWwiseRoot 'Scripts\Build\Plugins\wp.py'
        PythonPath           = if ($python) { $python.Path } else { $null }
        PythonPrefixArguments = if ($python) { [string[]]$python.PrefixArguments } else { [string[]]@() }
        CMakePath            = if ($RequireCMake) { Resolve-RequiredCommand -Name 'cmake.exe' -Purpose 'configuring and building the shared Core' } else { $null }
        CTestPath            = if ($RequireCTest) { Resolve-RequiredCommand -Name 'ctest.exe' -Purpose 'running the shared Core tests' } else { $null }
        MSBuildPath          = if ($RequireMSBuild) { Resolve-MSBuildPath } else { $null }
    }
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$Description
    )

    Write-Host "==> $Description"
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Invoke-Wp {
    param(
        [Parameter(Mandatory = $true)]$Environment,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $pythonArguments = @($Environment.PythonPrefixArguments) + @($Environment.WpPy) + $ArgumentList
    Invoke-CheckedCommand -FilePath $Environment.PythonPath -ArgumentList $pythonArguments -Description $Description
}

function Get-WwiseGeneratedProjects {
    param([Parameter(Mandatory = $true)][string]$PluginRoot)

    return @(
        Join-Path $PluginRoot 'SoundEnginePlugin\RealWorldWeatherAcoustics_Windows_vc170_static.vcxproj'
        Join-Path $PluginRoot 'SoundEnginePlugin\RealWorldWeatherAcoustics_Windows_vc170_shared.vcxproj'
        Join-Path $PluginRoot 'WwisePlugin\RealWorldWeatherAcoustics_Authoring_Windows_vc170.vcxproj'
    )
}

function Assert-WwiseProjectIsolation {
    param([Parameter(Mandatory = $true)][string]$PluginRoot)

    $projects = Get-WwiseGeneratedProjects -PluginRoot $PluginRoot
    foreach ($project in $projects) {
        if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
            throw "Generated Wwise project is missing: '$project'. Run Scripts\Configure.ps1."
        }

        $document = New-Object System.Xml.XmlDocument
        $document.PreserveWhitespace = $true
        $document.Load($project)

        $pathNodes = @($document.SelectNodes("//*[local-name()='OutDir' or local-name()='IntDir']"))
        if ($pathNodes.Count -eq 0) {
            throw "Generated project '$project' has no OutDir/IntDir nodes and cannot be isolation-validated."
        }
        foreach ($node in $pathNodes) {
            if ($node.InnerText -notlike '*Build\Wwise\*') {
                throw "Generated project '$project' still has a non-local $($node.LocalName): '$($node.InnerText)'. Run Scripts\Configure.ps1."
            }
        }

        $pluginDependencyNodes = @($document.SelectNodes("//*[local-name()='AdditionalDependencies']") |
            Where-Object { $_.InnerText -like '*RealWorldWeatherAcousticsSource.lib*' })
        if ($project -notlike '*_static.vcxproj' -and $pluginDependencyNodes.Count -eq 0) {
            throw "Generated project '$project' has no Runtime plug-in library dependency and cannot be isolation-validated."
        }
        foreach ($node in $pluginDependencyNodes) {
            if ($node.InnerText -like '*RealWorldWeatherAcousticsSource.lib*' -and $node.InnerText -notlike '*Build\Wwise\Runtime\*') {
                throw "Generated project '$project' still links the plug-in runtime library outside Build\Wwise: '$($node.InnerText)'."
            }
        }

        $factoryCommands = @($document.SelectNodes("//*[local-name()='PostBuildEvent']/*[local-name()='Command']") |
            Where-Object { $_.InnerText -like '*RealWorldWeatherAcousticsSourceFactory.h*' })
        if ($project -like '*_Authoring_*.vcxproj' -and $factoryCommands.Count -eq 0) {
            throw "Generated Authoring project '$project' has no factory-header PostBuildEvent."
        }
        foreach ($node in $factoryCommands) {
            if ($node.InnerText -like '*RealWorldWeatherAcousticsSourceFactory.h*' -and $node.InnerText -notlike '*Artifacts\Runtime\include\AK\Plugin*') {
                throw "Generated project '$project' still stages the factory header outside Artifacts\Runtime."
            }
        }
    }
}

function Assert-ExpectedBuildOutputs {
    param(
        [Parameter(Mandatory = $true)][string]$ProductRoot,
        [string]$Configuration = 'Release'
    )

    $expected = @(
        Join-Path $ProductRoot "Build\Wwise\Runtime\x64_vc170\$Configuration\lib\RealWorldWeatherAcousticsSource.lib"
        Join-Path $ProductRoot "Build\Wwise\Runtime\x64_vc170\$Configuration\bin\RealWorldWeatherAcoustics.dll"
        Join-Path $ProductRoot "Build\Wwise\Authoring\x64\$Configuration\bin\Plugins\RealWorldWeatherAcoustics.dll"
        Join-Path $ProductRoot "Build\Wwise\Authoring\x64\$Configuration\bin\Plugins\RealWorldWeatherAcoustics.xml"
        Join-Path $ProductRoot 'Artifacts\Runtime\include\AK\Plugin\RealWorldWeatherAcousticsSourceFactory.h'
        Join-Path $ProductRoot 'Artifacts\Runtime\include\AK\Plugin\RealWorldWeatherAcousticsRuntimeAPI.h'
    )

    foreach ($path in $expected) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Expected build output is missing: '$path'."
        }
        if ((Get-Item -LiteralPath $path).Length -eq 0) {
            throw "Expected build output is empty: '$path'."
        }
    }
}
