[CmdletBinding()]
param(
    [string]$ProductRoot,
    [string]$PluginRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

if (-not $ProductRoot) {
    $ProductRoot = Get-ProductRoot
}
if (-not $PluginRoot) {
    $PluginRoot = Join-Path $ProductRoot 'RealWorldWeatherAcoustics'
}

$resolvedProductRoot = (Resolve-Path -LiteralPath $ProductRoot).Path
$resolvedPluginRoot = (Resolve-Path -LiteralPath $PluginRoot).Path
[void](Assert-PathUnderRoot -Path $resolvedPluginRoot -Root $resolvedProductRoot -Description 'Wwise plug-in project root')

$localRuntimeRoot = '$(MSBuildThisFileDirectory)..\..\Build\Wwise\Runtime'
$localAuthoringRoot = '$(MSBuildThisFileDirectory)..\..\Build\Wwise\Authoring'
$factoryDestination = '$(MSBuildThisFileDirectory)..\..\Artifacts\Runtime\include\AK\Plugin'
$factorySource = '$(MSBuildThisFileDirectory)..\SoundEnginePlugin\RealWorldWeatherAcousticsSourceFactory.h'

function Save-XmlDocument {
    param(
        [Parameter(Mandatory = $true)][System.Xml.XmlDocument]$Document,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $settings.Indent = $false
    $settings.NewLineChars = "`r`n"
    $settings.NewLineHandling = [System.Xml.NewLineHandling]::Replace
    $writer = [System.Xml.XmlWriter]::Create($Path, $settings)
    try {
        $Document.Save($writer)
    }
    finally {
        $writer.Dispose()
    }
}

function Update-PluginDependencies {
    param(
        [Parameter(Mandatory = $true)][System.Xml.XmlDocument]$Document,
        [Parameter(Mandatory = $true)][string]$ProjectPath
    )

    $updated = 0
    $platformMarker = '$(Platform)_vc170\'
    foreach ($node in $Document.SelectNodes("//*[local-name()='AdditionalDependencies']")) {
        $entries = @($node.InnerText -split ';')
        $changed = $false
        for ($index = 0; $index -lt $entries.Count; $index++) {
            if ($entries[$index] -notlike '*RealWorldWeatherAcousticsSource.lib') {
                continue
            }

            $markerIndex = $entries[$index].IndexOf($platformMarker, [System.StringComparison]::OrdinalIgnoreCase)
            if ($markerIndex -lt 0) {
                throw "Cannot safely rewrite the runtime plug-in dependency '$($entries[$index])' in '$ProjectPath'."
            }

            $tail = $entries[$index].Substring($markerIndex)
            $entries[$index] = "$localRuntimeRoot\$tail"
            $changed = $true
            $updated++
        }

        if ($changed) {
            $node.InnerText = $entries -join ';'
        }
    }

    if ($updated -eq 0) {
        throw "No RealWorldWeatherAcousticsSource.lib dependency was found in '$ProjectPath'. The generated project layout may have changed."
    }
}

function Update-WwiseProject {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectPath,
        [Parameter(Mandatory = $true)][ValidateSet('RuntimeStatic', 'RuntimeShared', 'Authoring')][string]$Kind
    )

    if (-not (Test-Path -LiteralPath $ProjectPath -PathType Leaf)) {
        throw "Generated project is missing: '$ProjectPath'."
    }

    $document = New-Object System.Xml.XmlDocument
    $document.PreserveWhitespace = $true
    $document.Load($ProjectPath)

    switch ($Kind) {
        'RuntimeStatic' { $outDir = "$localRuntimeRoot\`$(Platform)_vc170\`$(Configuration)\lib\" }
        'RuntimeShared' { $outDir = "$localRuntimeRoot\`$(Platform)_vc170\`$(Configuration)\bin\" }
        'Authoring'     { $outDir = "$localAuthoringRoot\`$(Platform)\`$(Configuration)\bin\Plugins\" }
    }

    if ($Kind -eq 'Authoring') {
        $intDir = "$localAuthoringRoot\`$(Platform)\`$(Configuration)\obj\`$(ProjectName)\"
    }
    else {
        $intDir = "$localRuntimeRoot\`$(Platform)_vc170\`$(Configuration)\obj\`$(ProjectName)\"
    }

    $outNodes = @($document.SelectNodes("//*[local-name()='OutDir']"))
    $intNodes = @($document.SelectNodes("//*[local-name()='IntDir']"))
    if ($outNodes.Count -eq 0 -or $intNodes.Count -eq 0) {
        throw "Generated project '$ProjectPath' does not contain the expected OutDir/IntDir nodes."
    }
    foreach ($node in $outNodes) { $node.InnerText = $outDir }
    foreach ($node in $intNodes) { $node.InnerText = $intDir }

    if ($Kind -in @('RuntimeShared', 'Authoring')) {
        Update-PluginDependencies -Document $document -ProjectPath $ProjectPath
    }

    if ($Kind -eq 'Authoring') {
        $commands = @($document.SelectNodes("//*[local-name()='PostBuildEvent']/*[local-name()='Command']") |
            Where-Object { $_.InnerText -like '*RealWorldWeatherAcousticsSourceFactory.h*' })
        if ($commands.Count -eq 0) {
            throw "No factory-header PostBuildEvent was found in '$ProjectPath'."
        }

        $command = "if not exist `"$factoryDestination`" mkdir `"$factoryDestination`"`r`ncopy /y `"$factorySource`" `"$factoryDestination\RealWorldWeatherAcousticsSourceFactory.h`""
        foreach ($node in $commands) {
            $node.InnerText = $command
        }
    }

    Save-XmlDocument -Document $document -Path $ProjectPath
    Write-Host "Patched $Kind project: $ProjectPath"
}

$projects = @{
    RuntimeStatic = Join-Path $resolvedPluginRoot 'SoundEnginePlugin\RealWorldWeatherAcoustics_Windows_vc170_static.vcxproj'
    RuntimeShared = Join-Path $resolvedPluginRoot 'SoundEnginePlugin\RealWorldWeatherAcoustics_Windows_vc170_shared.vcxproj'
    Authoring = Join-Path $resolvedPluginRoot 'WwisePlugin\RealWorldWeatherAcoustics_Authoring_Windows_vc170.vcxproj'
}

Update-WwiseProject -ProjectPath $projects.RuntimeStatic -Kind RuntimeStatic
Update-WwiseProject -ProjectPath $projects.RuntimeShared -Kind RuntimeShared
Update-WwiseProject -ProjectPath $projects.Authoring -Kind Authoring
Assert-WwiseProjectIsolation -PluginRoot $resolvedPluginRoot

Write-Host 'Generated Wwise projects are isolated under Build\Wwise and Artifacts\Runtime.'
