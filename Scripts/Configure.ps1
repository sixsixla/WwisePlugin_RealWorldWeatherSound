[CmdletBinding()]
param(
    [string]$WwiseRoot,
    [ValidateSet('vc170')][string]$Toolset = 'vc170'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

$environment = Resolve-WeatherEnvironment -WwiseRoot $WwiseRoot -RequirePython -RequireMSBuild
if (-not (Test-Path -LiteralPath $environment.PluginRoot -PathType Container)) {
    throw "Wwise plug-in project root is missing: '$($environment.PluginRoot)'."
}

Push-Location $environment.PluginRoot
try {
    Invoke-Wp -Environment $environment -ArgumentList @('premake', 'Windows_vc170', '-t', $Toolset) -Description 'Generate Windows Runtime projects'
    Invoke-Wp -Environment $environment -ArgumentList @('premake', 'Authoring_Windows', '-t', $Toolset) -Description 'Generate Windows Authoring projects'
}
finally {
    Pop-Location
}

& (Join-Path $PSScriptRoot 'Patch-WwiseProjects.ps1') -ProductRoot $environment.ProductRoot -PluginRoot $environment.PluginRoot
Write-Host 'Configuration complete. No output or intermediate directory points into the Wwise installation.'
