[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InputMp3,
    [string]$OutputWav,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')

$productRoot = Get-ProductRoot
if (-not (Test-Path -LiteralPath $InputMp3 -PathType Leaf)) {
    throw "Input MP3 is missing: '$InputMp3'."
}
$inputPath = (Resolve-Path -LiteralPath $InputMp3).Path
if (-not [System.IO.Path]::GetExtension($inputPath).Equals(
    '.mp3',
    [System.StringComparison]::OrdinalIgnoreCase
)) {
    throw "-InputMp3 must identify an MP3 file: '$inputPath'."
}

if ([string]::IsNullOrWhiteSpace($OutputWav)) {
    $OutputWav = Join-Path $productRoot `
        'WwiseSmoke\RealWorldWeatherAcousticsSmoke\Originals\SFX\RWWA_Heavy_Rain_Puddles_30s.wav'
}
$outputPath = Assert-PathUnderRoot `
    -Path $OutputWav `
    -Root $productRoot `
    -Description 'Prepared rain test WAV'
if (-not [System.IO.Path]::GetExtension($outputPath).Equals(
    '.wav',
    [System.StringComparison]::OrdinalIgnoreCase
)) {
    throw "-OutputWav must end in .wav: '$outputPath'."
}
if ((Test-Path -LiteralPath $outputPath -PathType Leaf) -and -not $Force) {
    throw "Output WAV already exists. Pass -Force to replace it: '$outputPath'."
}

$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
$ffprobe = Get-Command ffprobe -ErrorAction SilentlyContinue
if (-not $ffmpeg -or -not $ffprobe) {
    throw 'ffmpeg and ffprobe must both be available on PATH.'
}
[void](New-Item -ItemType Directory -Path (Split-Path -Parent $outputPath) -Force)

$overwriteArgument = if ($Force) { '-y' } else { '-n' }
& $ffmpeg.Source `
    -hide_banner `
    -loglevel error `
    $overwriteArgument `
    -ss 0 `
    -i $inputPath `
    -t 30 `
    -map_metadata -1 `
    -vn `
    -c:a pcm_s24le `
    -ar 48000 `
    -ac 2 `
    $outputPath
if ($LASTEXITCODE -ne 0) {
    throw "ffmpeg failed with exit code $LASTEXITCODE."
}

$probeJson = & $ffprobe.Source `
    -v error `
    -show_entries 'format=duration,format_name,size,bit_rate:stream=codec_name,sample_rate,channels,channel_layout,bits_per_sample' `
    -of json `
    $outputPath
if ($LASTEXITCODE -ne 0) {
    throw "ffprobe failed with exit code $LASTEXITCODE."
}
$probe = $probeJson | ConvertFrom-Json
$stream = @($probe.streams)[0]
$duration = [double]::Parse(
    [string]$probe.format.duration,
    [System.Globalization.CultureInfo]::InvariantCulture
)
$assertions = [ordered]@{
    codecIsPcm24       = [string]$stream.codec_name -eq 'pcm_s24le'
    sampleRateIs48Khz  = [int]$stream.sample_rate -eq 48000
    channelCountIsTwo  = [int]$stream.channels -eq 2
    bitDepthIs24       = [int]$stream.bits_per_sample -eq 24
    durationIsThirty   = [Math]::Abs($duration - 30.0) -le (1.0 / 48000.0)
    outputIsNonEmpty   = (Get-Item -LiteralPath $outputPath).Length -gt 44
}
$failedAssertions = @(
    $assertions.GetEnumerator() |
        Where-Object { -not [bool]$_.Value } |
        ForEach-Object { $_.Key }
)
if ($failedAssertions.Count -gt 0) {
    throw "Prepared rain WAV assertions failed: $($failedAssertions -join ', ')."
}

[pscustomobject]@{
    success      = $true
    input        = $inputPath
    inputSha256  = (Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash.ToLowerInvariant()
    output       = $outputPath
    outputBytes  = (Get-Item -LiteralPath $outputPath).Length
    outputSha256 = (Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash.ToLowerInvariant()
    format       = [pscustomobject]@{
        codec         = [string]$stream.codec_name
        sampleRate    = [int]$stream.sample_rate
        channels      = [int]$stream.channels
        bitsPerSample = [int]$stream.bits_per_sample
        duration      = $duration
    }
    assertions   = $assertions
} | ConvertTo-Json -Depth 5
