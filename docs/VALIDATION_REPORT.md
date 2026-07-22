# RealWorld Weather Acoustics v0.2 Validation Report

## Verdict

The v0.2 executable slice passes its current automated acceptance boundary on Wwise `2023.1.19.8928`, Windows x64, Visual Studio 2022/vc170, Release.

This verdict covers shared Core/DSP tests, deterministic offline rendering, Wwise Source and Authoring binaries, minimal staging and installation, official Wwise property binding for the host-owned Geometry checkbox, disposable Wwise Authoring smoke project creation, GUI smoke for the new Feature editing controls, Transport playback, Voice/CPU observation, non-silent output, Profiler capture, and smoke cleanup.

It does not claim subjective listening approval, generated SoundBank loading/execution in an independent Native SoundEngine Host, or game-side engine integration.

## Implemented Boundary

- Programmatic, physically inspired wind/rain weather source; no recorded sample pack.
- Improved rain layer plus wind layer; still not a full physical fluid/acoustic solver.
- One Listener with position and yaw.
- Fixed 8 `SphereProxy` feature slots in Wwise PropertySet; `ActiveK=4` enters DSP.
- Metal, Wood, Glass, and Tile response profiles.
- Response masks: Disabled, Rain, Wind, Rain + Wind.
- Three Authoring presets: `Open Wind`, `Rain on Metal`, `Wind + Rain Ring`.
- Wwise Authoring 2D Preview supports Listener drag, Yaw drag, Feature Add, Feature center drag, radius-handle drag, Delete button, and Delete key.
- No Listener Path.
- No thunderstorm/lightning event layer.
- No Unity/Unreal adapter, game C ABI, Scene Snapshot transport, Capture/Replay, Monitor UI, Ambisonics, or cross-platform packaging in this slice.

## Fresh Verification Evidence

Primary final smoke evidence:

```text
Build/WwiseSmoke/wwise-authoring-smoke-20260722T035623735Z.json
Build/WwiseSmoke/wwise-authoring-smoke-20260722T035623735Z.prof
```

The final smoke was started at `2026-07-22T03:56:23.7387102Z` and finished at `2026-07-22T03:56:28.4149457Z`.

| Claim | Result | Evidence |
| --- | --- | --- |
| Final Wwise Authoring smoke completed | PASS | JSON `success = true`; wrapper `error = null` |
| Installed Authoring files match staging | PASS | Installed DLL/XML SHA-256 values match staged files and `Artifacts/stage-record.json` |
| Fixture smoke project unchanged | PASS | `fixtureUnchanged = true`; before/after file count `34`, bytes `613667`, digest unchanged |
| Disposable smoke copy isolated and removed | PASS | `copyMatchesFixtureBytes = true`; `disposableProjectRemoved = true`; `disposableProjectRetained = false`; cleanup errors `[]` |
| Plug-in discovered and instantiated | PASS | WAAPI created Source class ID `2031682562` in an isolated disposable project |
| Official Wwise populate binding works | PASS | Geometry checkbox is a host-synchronized Wwise control; smoke changed `GeometryEnabled` from `true` to `false` and Wwise Undo restored `true` |
| Inspector text matches stable preset | PASS | `inspectorTextMatchesStablePreset = true`; 13 visible Inspector edit texts matched `60`, `0.75`, `24681357`, `20`, `4 / 8`, `14`, `35`, `0.65`, and Feature1 `0`, `6`, `2.5`, `3`, `10` |
| Add initializes complete defaults | PASS | `addButtonInitializesFeature5Defaults = true`; new Feature5 defaults include Geometry enabled, X/Z from Listener yaw, Y `0`, Radius `2`, Profile `0`, Mask `3`, Priority `1` |
| GUI Add/drag/radius/Delete works | PASS | Add changed `FeatureCount` `4 -> 5`; center drag changed `Feature5X/Z`; radius drag changed `Feature5Radius`; Delete button changed `FeatureCount` `5 -> 4`; Delete key also changed `5 -> 4` |
| GUI Undo gates work | PASS | Six explicit Undo gates restored Geometry, Add, center drag, radius drag, Delete button, and Delete key properties |
| Wwise SoundBank generation serializes Authoring parameters | PASS | Geometry true/false SoundBanks are each `457` bytes; each contains the expected `273`-byte parameter block exactly once at bank offset `77`; `GeometryEnabled` is block offset `16` / bank offset `93`; true byte `1`, false byte `0`; blocks differ only at offset `16`; generation logs are only `Message`; Geometry restored `true` |
| Transport executes the real Source | PASS | `transportState = playing`; isolated transport evidence `true` |
| Real physical voice exists | PASS | `voicesTotal = 1`; `voicesPhysical = 1`; `voicesVirtual = 0` |
| Output is non-silent | PASS | `OutputPeak = -28.6425437927246 dB`, above the `-90 dB` silence floor |
| Plug-in executes in Wwise Profiler | PASS | Source row `RealWorld Weather Acoustics`: 1 instance, `0.1396999955177307 ms` exclusive CPU |
| Profiler capture saved | PASS | `.prof` saved at `Build/WwiseSmoke/wwise-authoring-smoke-20260722T035623735Z.prof`; size `368867` bytes |

Additional automated evidence from `Build/Core/Testing/Temporary/LastTest.log`:

| Claim | Result | Evidence |
| --- | --- | --- |
| Core/weather DSP tests | PASS | `rwwa_core_tests` passed; log includes rain/wind/gust metrics |
| Bank parameter ABI contract | PASS | `rwwa_source_params_bank_tests` passed |
| Offline `weather-ring` fixture | PASS | `rwwa_offline_renderer_weather_ring` wrote `Build/Core/Fixtures/weather_ring.wav` |
| Offline `open-wind` fixture | PASS | `rwwa_offline_renderer_open_wind` wrote `Build/Core/Fixtures/open_wind.wav` |
| Offline `rain-metal` fixture | PASS | `rwwa_offline_renderer_rain_metal` wrote `Build/Core/Fixtures/rain_metal.wav` |

The final staged files recorded in `Artifacts/stage-record.json` are:

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| Authoring `RealWorldWeatherAcoustics.dll` | 86,528 | `0fbb33d4ae7d8066d91b5ec58def7933bb98388cdf8e961cbc24a6a41cd9597e` |
| Authoring `RealWorldWeatherAcoustics.xml` | 29,000 | `662cd1cf63ddd88b8ae3ae75c47bc44df790b7fa42c8f63257e6892c32d2cea5` |
| Runtime `RealWorldWeatherAcousticsSource.lib` | 273,182 | `985b211391aff17c9716d899eeec448ae27c09b14dc7b4f57ff4fabd9d49d7d6` |
| Runtime `RealWorldWeatherAcousticsSourceFactory.h` | 1,512 | `d7227f57c422dee6dab622fe6f59528c0d2685ae2a3b055e20b2aca0ae8219c6` |

## What This Does Not Prove

- No human subjective listening acceptance was completed in this validation pass. The generated WAV files, DSP metrics, output peak, and any spectrum/envelope checks are regression evidence only; they are not listening approval.
- The 261-byte legacy and 273-byte current parameter ABI tests prove `SetParamsBlock` accepts the exact old/new layouts, rejects off-size blocks, and maps the appended wind fields correctly. The canonical smoke additionally proves Wwise actual SoundBank generation and Authoring parameter serialization for the generated true/false Geometry banks. It does not prove those generated banks load and execute through a standalone Native SoundEngine Host.
- The Wwise smoke proves Authoring discovery, Source creation, GUI mutation/Undo, SoundBank generation/parameter serialization, Transport playback, Voice/CPU evidence, non-silent output, Profiler capture, fixture isolation, and cleanup. It does not prove game runtime C ABI, Scene Snapshot transport, Unity/Unreal adapters, platform packaging, or commercial release identity.

## Reproduction

Run from the product root after closing other Wwise Authoring instances:

```powershell
$wwise = 'E:\WwiseSoft2023\Wwise_2023.1.19.8928'

& .\Scripts\Resolve-Environment.ps1 -WwiseRoot $wwise
& .\Scripts\Configure.ps1 -WwiseRoot $wwise
& .\Scripts\Build.ps1 -WwiseRoot $wwise
& .\Scripts\Test.ps1 -WwiseRoot $wwise
& .\Scripts\Stage.ps1
& .\Scripts\Install-WwiseAuthoring.ps1 -WwiseRoot $wwise -Apply
& .\Scripts\Smoke-WwiseAuthoring.ps1 `
    -WwiseRoot $wwise `
    -PythonWithWaapi 'D:\Tool\Wwise_mcp\.venv\Scripts\python.exe'
```

`-PythonWithWaapi` is local smoke-test tooling only. It is not a product build or runtime dependency.

## Remaining Risks

- A human listening pass is still needed for subjective wind/rain/material quality.
- Standalone Native SoundEngine Host loading/executing the generated SoundBank remains the next runtime proof point.
- Fixed 8-slot Authoring storage is a compatibility layer. Delete-left-shift can change slot-derived IDs; stable external feature identity belongs to the later registry/snapshot design.
- Development IDs remain internal only: `CompanyID=64`, `PluginID=31001`. Public or commercial distribution requires Audiokinetic-assigned non-conflicting IDs and a full rebuild/revalidation.
