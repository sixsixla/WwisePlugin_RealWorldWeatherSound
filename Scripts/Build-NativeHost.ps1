[CmdletBinding()]
param(
    [string]$WwiseRoot = 'E:\WwiseSoft2023\Wwise_2023.1.19.8928'
)

$ErrorActionPreference = 'Stop'

$productRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceRoot = Join-Path $productRoot 'Tools\NativeHost'
$buildRoot = Join-Path $productRoot 'Build\NativeHost'
$sdkHeader = Join-Path $WwiseRoot 'SDK\include\AK\SoundEngine\Common\AkSoundEngine.h'
$sdkLibrary = Join-Path $WwiseRoot 'SDK\x64_vc170\Release\lib\AkSoundEngine.lib'

foreach ($requiredPath in @($sourceRoot, $sdkHeader, $sdkLibrary)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required Native Host build input is missing: '$requiredPath'."
    }
}

$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmakeCommand) {
    $cmakeFallback = 'C:\Program Files\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $cmakeFallback -PathType Leaf) {
        $cmakePath = $cmakeFallback
    }
    else {
        throw 'CMake 3.20 or newer was not found on PATH or under C:\Program Files\CMake\bin.'
    }
}
else {
    $cmakePath = $cmakeCommand.Source
}
$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestPath -PathType Leaf)) {
    throw "CTest was not found next to CMake: '$ctestPath'."
}

$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
    throw "Visual Studio locator is missing: '$vswherePath'."
}
$visualStudioRoot = & $vswherePath `
    -latest `
    -version '[17.0,18.0)' `
    -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($visualStudioRoot)) {
    throw 'Visual Studio 2022 with the Desktop C++ toolchain was not found.'
}
$msbuildPath = Join-Path $visualStudioRoot.Trim() 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuildPath -PathType Leaf)) {
    throw "MSBuild was not found under the Visual Studio 2022 installation: '$msbuildPath'."
}

[void](New-Item -ItemType Directory -Path $buildRoot -Force)

& $cmakePath `
    -S $sourceRoot `
    -B $buildRoot `
    -G 'Visual Studio 17 2022' `
    -A x64 `
    "-DRWWA_WWISE_ROOT=$WwiseRoot"
if ($LASTEXITCODE -ne 0) {
    throw "Native Host CMake configure failed with exit code $LASTEXITCODE."
}

& $cmakePath `
    --build $buildRoot `
    --config Release `
    --target rwwa_native_host rwwa_native_host_scene_payload_tests `
    -- /m
if ($LASTEXITCODE -ne 0) {
    throw "Native Host build failed with exit code $LASTEXITCODE."
}

& $ctestPath --test-dir $buildRoot -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "Native Host helper tests failed with exit code $LASTEXITCODE."
}

$executablePath = Join-Path $buildRoot 'bin\Release\rwwa_native_host.exe'
if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "Native Host build completed without the expected executable: '$executablePath'."
}

[ordered]@{
    success          = $true
    configuration    = 'Release'
    platform         = 'x64'
    toolset          = 'vc170 / Visual Studio 2022'
    cmake             = $cmakePath
    msbuild          = $msbuildPath
    helperTests      = 'passed'
    wwiseRoot        = [System.IO.Path]::GetFullPath($WwiseRoot)
    executable       = [System.IO.Path]::GetFullPath($executablePath)
} | ConvertTo-Json -Depth 3
