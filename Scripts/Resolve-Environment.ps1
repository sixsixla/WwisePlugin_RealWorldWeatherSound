[CmdletBinding()]
param(
    [string]$WwiseRoot,
    [switch]$AsJson
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

$environment = Resolve-WeatherEnvironment `
    -WwiseRoot $WwiseRoot `
    -RequirePython `
    -RequireCMake `
    -RequireCTest `
    -RequireMSBuild

if ($AsJson) {
    $environment | ConvertTo-Json -Depth 4
}
else {
    $environment | Format-List
}
