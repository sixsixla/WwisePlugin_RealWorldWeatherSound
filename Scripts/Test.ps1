[CmdletBinding()]
param(
    [string]$WwiseRoot,
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [switch]$SkipArtifactChecks
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

$syntaxFailures = New-Object 'System.Collections.Generic.List[string]'
foreach ($script in Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' -File) {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($script.FullName, [ref]$tokens, [ref]$errors)
    foreach ($parseError in $errors) {
        $syntaxFailures.Add("$($script.Name):$($parseError.Extent.StartLineNumber): $($parseError.Message)")
    }
}
if ($syntaxFailures.Count -gt 0) {
    throw "PowerShell syntax validation failed:`r`n$($syntaxFailures -join "`r`n")"
}
Write-Host 'PowerShell syntax validation passed.'

$environment = Resolve-WeatherEnvironment -WwiseRoot $WwiseRoot -RequireCTest
$cmakeProject = Join-Path $environment.ProductRoot 'CMakeLists.txt'
if (Test-Path -LiteralPath $cmakeProject -PathType Leaf) {
    $coreBuild = Assert-PathUnderRoot -Path (Join-Path $environment.BuildRoot 'Core') -Root $environment.ProductRoot -Description 'Core build directory'
    if (-not (Test-Path -LiteralPath $coreBuild -PathType Container)) {
        throw "Core build directory is missing: '$coreBuild'. Run Scripts\Build.ps1 first."
    }
    if (-not (Get-ChildItem -LiteralPath $coreBuild -Recurse -Filter 'CTestTestfile.cmake' -File | Select-Object -First 1)) {
        throw "No CTestTestfile.cmake exists under '$coreBuild'. Configure with tests enabled and rebuild."
    }

    Invoke-CheckedCommand -FilePath $environment.CTestPath -ArgumentList @(
        '--test-dir', $coreBuild,
        '-C', $Configuration,
        '--output-on-failure'
    ) -Description 'Run shared Core and offline renderer tests'
}
else {
    Write-Warning 'No root CMakeLists.txt exists; Core tests were skipped.'
}

Assert-WwiseProjectIsolation -PluginRoot $environment.PluginRoot
if (-not $SkipArtifactChecks) {
    Assert-ExpectedBuildOutputs -ProductRoot $environment.ProductRoot -Configuration $Configuration
}

Write-Host 'Validation passed.'
