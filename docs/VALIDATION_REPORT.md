# RealWorld Weather Acoustics v0.3 Validation Report

## Verdict

The v0.3 hybrid vertical slice passes its current automated acceptance boundary on Wwise `2023.1.19.8928`, Windows x64, Visual Studio 2022/vc170, Release.

This verdict covers:

- v0.2 Source `PluginID=31001` compatibility regression: 69 parameters, 261-byte legacy block, 273-byte current block.
- v0.3 Effect `PluginID=31002`: 71 parameters, 281-byte block, Authoring creation/serialization, three SoundBank variants, Profiler CPU/output evidence.
- Retained rain demo: persistent Wwise Sound/Event using a 30-second high-quality rain WAV as the Effect input source.
- Runtime Scene C ABI: `SetV1` / `GetV1` / `ClearV1`, 392-byte scene, 40-byte feature, 8 feature slots, 89-field full-payload comparison, and UNCLAIMED-only authored fallback.
- Runtime Diagnostics V1: 96-byte ABI, `ResetV1` / `GetV1`, coherent snapshots, BUSY retry, execute/frame/selection/reason counters, actual peaks and non-finite samples, plus a coherent five-field last-block tuple committed without waiting.
- Native SoundEngine Host three-state audio-contract matrix plus two expected-failure negative gates.

It does not claim subjective listening approval, Unity/Unreal integration, commercial release identity, or future advanced DSP parameters.

## Implemented Boundary

### Source 31001 retained boundary

- Programmatic, physically inspired wind/rain Source remains available.
- Source parameter count remains 69.
- Source Bank ABI regression keeps 261-byte legacy and 273-byte current blocks.
- Wwise Authoring 2D Preview continues to support Listener drag, yaw drag, Feature Add, Feature center drag, radius-handle drag, Delete button, and Delete key.

### Effect 31002 v0.3 boundary

- Effect attaches to Wwise Audio File Source/streamed loop paths on Sound, Actor-Mixer, or Bus.
- Input material provides the high-quality rain/wind/ambience bed; the plug-in adds geometry/material interaction only.
- Actual current parameters:

| Parameter | Range | Default |
| --- | --- | --- |
| `InputRole` | `Rain=0`, `Wind=1`, `Generic=2` | `Generic=2` |
| `WetMix` | `0..1` | `0` |
| `ResponseGainDb` | `-24..12` dB | `0` |
| `TransientSensitivity` | `0..1` | `0.5` |
| `RainIntensity` | `0..1` | Source-compatible default |
| `WindSpeed` | `0..40` m/s | Source-compatible default |
| `WindDirectionDegrees` | `0..360` | Source-compatible default |
| `WindGustiness` | `0..1` | Source-compatible default |
| `Seed` | `0..2147483647` | Source-compatible default |
| `GeometryEnabled` | bool | true |
| `ListenerX/Y/Z` | finite float | `0` |
| `ListenerYawDegrees` | degrees | `0` |
| `FeatureCount` | `0..8` | `4` |
| `Feature1..8 X/Y/Z/Radius/Profile/Mask/Priority` | fixed 8 slots | fixed ring defaults; Radius min clamp is 0.2 |

`Priority` is a numeric `0..1000` weight. It is not a four-level enum.

Not implemented in v0.3: `EnvelopeSensitivity`, band weights, smoothing, distance scale, priority bias, Unity Adapter, Unreal Adapter, advanced geometry types, Capture/Replay, Monitor UI, Ambisonics, and manual subjective listening acceptance.

### Retained rain demo boundary

The smoke project now keeps a human-openable Effect demo instead of relying only on disposable test objects:

| Object | Purpose |
| --- | --- |
| `RWWA_Demo_Heavy_Rain_Puddles` | Persistent Wwise Sound for manual audition |
| `RWWA_Demo_Heavy_Rain_Puddles_Audio` | Standard Wwise `AudioFileSource` referencing the retained WAV |
| `RWWA_Demo_Weather_Geometry_Effect` | `RealWorld Weather Acoustics Effect` on the demo Sound |
| `Play_RWWA_Demo_Heavy_Rain_Puddles` | Persistent Event targeting the demo Sound |

The source MP3 is the user-provided Envato preview at `C:\Users\Administrator\Downloads\Envato_Rain_Previews\02_Heavy_Rain_With_Puddles_YDZSTXK_preview.mp3`. `Scripts\Prepare-RainTestAsset.ps1` cuts the first 30 seconds and writes:

```text
WwiseSmoke/RealWorldWeatherAcousticsSmoke/Originals/SFX/RWWA_Heavy_Rain_Puddles_30s.wav
```

Verified WAV format: 48 kHz stereo PCM24, 30.0 seconds, `8640102` bytes, SHA-256 `1fa150708de00627796a4e3963a9becd97595e01e5e769e6a3b0a5d1cf076adc`. The generated test WAV and generated WEM are repository test assets so a fresh clone can immediately audition and validate the retained rain demo. Publishers must confirm that the Envato preview license covers repository distribution of those derived test assets.

Final persistent import report:

```text
Build/WwiseSmoke/import-rain-demo-20260722T121122186Z.json
```

## Fresh Verification Evidence

### Build and CTest

Primary CTest evidence:

```text
Build/Core/Testing/Temporary/LastTest.log
```

`Test 8/8` passed:

| Test | Result | Evidence |
| --- | --- | --- |
| `rwwa_core_tests` | PASS | rain/wind/gust metrics logged |
| `rwwa_geometry_interaction_tests` | PASS | includes `WetMix=0` sample transparency and role/geometry interaction checks |
| `rwwa_runtime_scene_api_tests` | PASS | 89-field scene payload, first-claim/BUSY behavior, UNCLAIMED-only fallback, coherent Diagnostics/Reset BUSY retry, non-finite counter, long-run `uint64_t` behavior, forced-contention last-tuple coherence, deterministic multi-writer encoding, and concurrency |
| `rwwa_source_params_bank_tests` | PASS | `SourceParams bank ABI contract passed: 261-byte legacy and 273-byte current blocks.` |
| `rwwa_effect_params_bank_tests` | PASS | `EffectParams bank ABI contract passed: 281-byte block, defaults, mapping, rejects, and clamps.` |
| `rwwa_offline_renderer_weather_ring` | PASS | wrote `Build/Core/Fixtures/weather_ring.wav` |
| `rwwa_offline_renderer_open_wind` | PASS | wrote `Build/Core/Fixtures/open_wind.wav` |
| `rwwa_offline_renderer_rain_metal` | PASS | wrote `Build/Core/Fixtures/rain_metal.wav` |

### Staging and install

`Artifacts/stage-record.json` records `stagedAtUtc = 2026-07-22T09:46:33.2694904Z` and exactly 5 staged files:

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| Authoring `RealWorldWeatherAcoustics.dll` | 128000 | `d87395eef53dff0ccbf1c3d8d287961dcd87da9edddb39de2ef03ab7f968349d` |
| Authoring `RealWorldWeatherAcoustics.xml` | 55101 | `275921b2ae9cd5fddddb219275a171afd8bb03d16d6793489fda9c383b412d96` |
| Runtime `RealWorldWeatherAcousticsSource.lib` | 577940 | `8c55f0175cc9d5e9b96cecd7b6327c0616697f386faeb524e7b3ed0c7a6296ac` |
| Runtime `RealWorldWeatherAcousticsSourceFactory.h` | 1614 | `04417df0762ae809a6d2848cc535fc3b042307c079d7fbe4cc148bd88e35a389` |
| Runtime `RealWorldWeatherAcousticsRuntimeAPI.h` | 5545 | `81de6f82591ce468325f0569d226a45627e5bdcd2b9b9df614bb65993bb8c2d6` |

Wwise Authoring installation copies only the DLL/XML. Latest backup:

```text
Artifacts/InstallBackup/20260722T094813054Z
```

### Wwise Authoring smoke

Primary final smoke evidence:

```text
Build/WwiseSmoke/wwise-authoring-smoke-20260722T123034276Z.json
Build/WwiseSmoke/wwise-authoring-smoke-20260722T123034276Z.prof
```

The final smoke started at `2026-07-22T12:17:07.4721631Z` and finished at `2026-07-22T12:17:14.3505421Z`.

| Claim | Result | Evidence |
| --- | --- | --- |
| Wwise Authoring smoke completed | PASS | wrapper `success = true`; client `success = true`; `error = null` |
| Installed Authoring files match staging | PASS | DLL/XML hashes match `Artifacts/stage-record.json` |
| Retained rain demo required | PASS | `requireRetainedRainDemo = true` |
| Retained rain demo template valid | PASS | Sound, AudioFileSource, Effect, Event, and Event target assertions are true |
| Effect smoke uses retained template | PASS | `effectObjectSet.mode = existing-template`; Sound `RWWA_Demo_Heavy_Rain_Puddles`; Effect `RWWA_Demo_Weather_Geometry_Effect` |
| Retained input WAV valid | PASS | `RWWA_Heavy_Rain_Puddles_30s.wav`; 48 kHz stereo PCM24; 30.0 s; `8640102` bytes; SHA-256 `1fa150708de00627796a4e3963a9becd97595e01e5e769e6a3b0a5d1cf076adc` |
| Fixture smoke project unchanged | PASS | `fixtureUnchanged = true`; 35 files, `9265832` bytes, digest `d9adddd0a32fb4fbe7073d146438140449e09feed403a91ed431d6f9019d9bd6` |
| Disposable copy isolated | PASS | `copyMatchesFixtureBytes = true`; `disposableProjectRemoved = true` |
| Source discovered and instantiated | PASS | Source class ID `2031682562` |
| Effect discovered and instantiated | PASS | Effect class ID `2031748099`, `PluginID=31002` |
| Source GUI smoke remains valid | PASS | all 36/36 GUI assertions; stable preset text, Add/Delete, window-message move/resize converged, radius, Delete key, Undo gates |
| Effect Priority edit works | PASS | `Feature1Priority` `10 -> 107 -> Undo 10` |
| Source Bank serialization remains valid | PASS | 69 parameters, 273-byte current block, 261-byte legacy regression retained |
| Effect Bank serialization works | PASS | 71 properties, 281-byte block, `InputRole`/`WetMix`/`GeometryEnabled` variant bytes match |
| Three Effect bank variants retained | PASS | Baseline, InputRoleWetGeometry, WetZero; each 473 bytes with a 281-byte Effect block |
| Fixture manifest written | PASS | `Build/NativeHost/Fixture/20260722T123034276Z/RWWA_Effect_Fixture.json`; 10/10 selected artifacts match source/destination size and SHA-256 |
| Assertion groups | PASS | Source 6/6, Effect 7/7, Shared 1/1 |
| Source executes | PASS | 1 physical voice; CPU `0.1414999962 ms`; peak `-26.45691872 dB` |
| Effect executes | PASS | 1 voice; CPU `0.1155999973 ms`; peak `-16.77216339 dB` |
| Profiler capture saved | PASS | `.prof` size `745058` bytes |

The retained fixture has 10 copied artifacts plus its manifest, 11 files total:

```text
Build/NativeHost/Fixture/20260722T123034276Z/RWWA_Effect_Fixture.json
Build/NativeHost/Fixture/20260722T123034276Z/RWWA_Effect_Baseline.bnk
Build/NativeHost/Fixture/20260722T123034276Z/RWWA_Effect_InputRoleWetGeometry.bnk
Build/NativeHost/Fixture/20260722T123034276Z/RWWA_Effect_WetZero.bnk
Build/NativeHost/Fixture/20260722T123034276Z/Init.bnk
Build/NativeHost/Fixture/20260722T123034276Z/Init.txt
Build/NativeHost/Fixture/20260722T123034276Z/PlatformInfo.xml
Build/NativeHost/Fixture/20260722T123034276Z/PluginInfo.xml
Build/NativeHost/Fixture/20260722T123034276Z/RWWA_Effect_WetZero.txt
Build/NativeHost/Fixture/20260722T123034276Z/SoundbanksInfo.xml
Build/NativeHost/Fixture/20260722T123034276Z/Media/528110025.wem
```

### Native Host audio-contract matrix

Runtime Scene V1 remains 392 bytes with eight 40-byte features. The Host compares all 89 payload fields: 17 scene/header/listener/weather fields and 9 fields for each feature. Runtime Diagnostics V1 is 96 bytes and is reset/read through `RWWA_RuntimeDiagnostics_ResetV1` / `RWWA_RuntimeDiagnostics_GetV1`. Its audio-thread publisher records measured wet difference and non-finite samples even when a bypass/off reason flag is set; flags do not overwrite measurements. Cumulative counters are `uint64_t` atomics.

Authored Bank/Authoring parameters are selected only when `RWWA_RuntimeScene_GetV1` returns `UNCLAIMED` and the Effect instance has never claimed a runtime scene. During first claim, `BUSY` selects a runtime-owned empty scene rather than authored fallback; after a snapshot exists, `BUSY` reuses that retained snapshot. Clear/error handling empties runtime state instead of silently selecting authored fallback.

Diagnostics Get returns only a coherent snapshot. The five fields `lastRuntimeSceneRevision`, `lastInputPeak`, `lastOutputPeak`, `lastWetDifferencePeak`, and `lastBlockUsedRuntimeScene` are a complete per-block tuple written with a no-wait try-commit. If publishers contend, the tuple may lag the cumulative counters, maxima, and generation, but it never combines fields from different blocks; every contending block still contributes to those cumulative values. Publish/reset handshake uses sequential consistency, and Get keeps generation as its final concurrency observation so a completed publisher cannot be missed after the payload read. If Get/Reset overlaps a publish or reset, it returns `RWWA_RUNTIME_STATUS_BUSY` without exposing partial state; control-thread callers retry. Deterministic tests cover forced try-commit contention, four-writer tuple encoding, generation changes during Get, publish/reset overlap, post-retry coherence, reset-to-zero, non-finite counting, concurrency, and long-run counter behavior.

| Report | Isolated condition | Diagnostics result |
| --- | --- | --- |
| `Build/NativeHost/native-host-rain-changed-20260722T123034276Z.json` | Baseline + enabled revision-2 runtime scene; `changed` | 110 executes / 56320 frames / runtime 110 / fallback 0 / wet 0 / geometry 0; max input `0.230743408`, output `0.227470636`, wet difference `0.0136105493` |
| `Build/NativeHost/native-host-rain-wet-bypass-20260722T123034276Z.json` | WetZero + enabled revision-2 runtime scene; `wet-bypass` | 112 executes / 57344 frames / runtime 112 / fallback 0 / wet 112 / geometry 0; input=output `0.230743408`, wet difference 0 |
| `Build/NativeHost/native-host-rain-geometry-disabled-20260722T123034276Z.json` | **Baseline** + disabled revision-3 runtime scene; `geometry-disabled` | 110 executes / 56320 frames / runtime 110 / fallback 0 / wet 0 / geometry 110; input=output `0.230743408`, wet difference 0 |

All three reports show Source 31001 and Effect 31002 registered, 89-field scene `Set/Get/Clear` full-payload match with mismatch count 0, Diagnostics `Reset/Get`, non-finite count 0, `Init.bnk` and requested-bank load/unload, successful PostEvent, render 0 failures, clean termination, and `exitStatus.success = true`. GeometryOff uses the Baseline bank rather than the InputRoleWetGeometry bank, so the disabled runtime scene is the only geometry-off variable.

The final CLI contract uses `-Expectation changed|wet-bypass|geometry-disabled`. `transparent` remains accepted only as a legacy mode; final acceptance uses reason-aware modes that gate both wet-difference magnitude and the relevant reason counter.

Negative evidence:

| Report | Deliberately wrong expectation | Required result |
| --- | --- | --- |
| Negative gate: changed-as-wet | Nonzero Baseline response asserted as `wet-bypass` | wet-difference and wet-bypass reason both fail; code 52 / `diagnostics-assertions` |
| Negative gate: geometry-as-wet | Geometry-disabled transparent output asserted as `wet-bypass` | difference is zero and wet magnitude passes, but reason counters fail; code 52 / `diagnostics-assertions` |

Both negative runs still have full-payload match, mismatch count 0, non-finite count 0, and clean cleanup. These failures show that neither a nonzero response nor an unrelated transparent reason can pass the matrix accidentally.

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
& .\Scripts\Build-NativeHost.ps1 -WwiseRoot $wwise
```

Native Host baseline:

```powershell
& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260722T123034276Z' `
    -Bank 'RWWA_Effect_Baseline.bnk' `
    -SceneJson 'Tools\NativeHost\scene.example.json' `
    -Expectation changed `
    -DurationMs 1200 `
    -SkipBuild
```

Native Host WetZero bypass:

```powershell
& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260722T123034276Z' `
    -Bank 'RWWA_Effect_WetZero.bnk' `
    -SceneJson 'Tools\NativeHost\scene.example.json' `
    -Expectation wet-bypass `
    -DurationMs 1200 `
    -SkipBuild
```

Native Host runtime GeometryOff:

```powershell
& .\Scripts\Smoke-WwiseNativeHost.ps1 `
    -WwiseRoot $wwise `
    -FixtureRoot 'Build\NativeHost\Fixture\20260722T123034276Z' `
    -Bank 'RWWA_Effect_Baseline.bnk' `
    -SceneJson 'Tools\NativeHost\scene.disabled.example.json' `
    -Expectation geometry-disabled `
    -DurationMs 1200 `
    -SkipBuild
```

`-PythonWithWaapi` is local smoke-test tooling only. It is not a product build or runtime dependency.

## What This Does Not Prove

- No human subjective listening acceptance was completed. WAV files, DSP metrics, output peak, Profiler CPU, and smoke reports are regression evidence only.
- Unity and Unreal adapters are not implemented or validated.
- Advanced DSP controls such as `EnvelopeSensitivity`, band weights, response smoothing, distance scale, and priority bias are not implemented.
- Current geometry remains fixed 8-slot sphere-style data for this slice. Plane, Box, Convex, Edge, Aperture, full 3D Authoring, Capture/Replay, and Monitor UI are future work.
- Development IDs remain internal only: `CompanyID=64`, Source `PluginID=31001`, Effect `PluginID=31002`. Public or commercial distribution requires Audiokinetic-assigned non-conflicting IDs and full rebuild/revalidation.

## Remaining Risks

- Subjective rain/wind/material quality still needs a human listening pass.
- Engine lifecycle and packaging risks remain for Unity/Unreal.
- The Runtime C ABI is validated in Native Host, but it has not been exercised through an actual game adapter.
- The current Effect relies on input material quality; poor loops or badly cut streamed assets will still sound poor.
